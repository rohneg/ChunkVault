#include <iostream>
#include <filesystem>
#include <string>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <random>
#include <CommonCrypto/CommonDigest.h>
#include <cstdio>
#include <sstream>
#include <vector>
#include "../database/Database.h"
#include "os_simulator.h"
#include "web_server.h"

namespace fs = std::filesystem;

const size_t BUFFER_SIZE = 4ULL * 1024ULL * 1024ULL;

uintmax_t calculateChunkSize(uintmax_t fileSize) {
    const uintmax_t MIN_CHUNK = 64ULL * 1024ULL;
    const uintmax_t MAX_CHUNK = 10ULL * 1024ULL * 1024ULL * 1024ULL;

    const double BASE_CHUNK = 256.0 * 1024.0 * 1024.0;
    const double BASE_FILE = 1024.0 * 1024.0 * 1024.0;
    const double GROWTH_FACTOR = 0.65;

    double chunkSize = BASE_CHUNK *
        std::pow(
            static_cast<double>(fileSize) / BASE_FILE,
            GROWTH_FACTOR
        );

    uintmax_t result = static_cast<uintmax_t>(chunkSize);

    result = std::max(result, MIN_CHUNK);
    result = std::min(result, MAX_CHUNK);
    result = std::min(result, fileSize);

    return result;
}


std::string generateFileId() {
    const std::string characters =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    std::random_device random;
    std::string id = "FILE_";

    for (int i = 0; i < 4; i++) {
        id += characters[random() % characters.size()];
    }

    return id;
}

std::string generateChunkId() {
    const std::string characters =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

    std::random_device random;
    std::string id = "CHUNK_";

    for (int i = 0; i < 4; i++) {
        id += characters[random() % characters.size()];
    }

    return id;
}

struct ChunkMetadata {
    std::string fileId;
    std::string chunkId;
    int chunkNumber;
    uintmax_t chunkSize;
    std::string node1Location;
    std::string node2Location;
    std::string checksum;

};

struct StoredChunk {
    std::string fileId;
    std::string fileName;
    std::string chunkPath;
    std::string node1Path;
    std::string node2Path;
    std::string checksum;
};

std::string calculateChecksum(const std::string& filePath) {
    std::ifstream file(filePath, std::ios::binary);

    if (!file) {
        return "";
    }

    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);

    char buffer[4096];

    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        CC_SHA256_Update(&context, buffer, static_cast<CC_LONG>(file.gcount()));
    }

    unsigned char hash[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(hash, &context);

    std::string checksum;

    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02x", hash[i]);
        checksum += hex;
    }

    return checksum;
}


std::string checkChunkStatus(const std::string& filePath,
                             const std::string& expectedChecksum) {
    if (!fs::exists(filePath) || !fs::is_regular_file(filePath)) {
        return "MISSING";
    }

    std::string actualChecksum = calculateChecksum(filePath);

    if (actualChecksum.empty()) {
        return "ERROR";
    }

    if (actualChecksum == expectedChecksum) {
        return "HEALTHY";
    }

    return "CORRUPTED";
}

void recoverChunk(const StoredChunk& chunk) {
    std::string source;

    if (checkChunkStatus(chunk.chunkPath, chunk.checksum) == "HEALTHY") {
        source = chunk.chunkPath;
    } else if (checkChunkStatus(chunk.node1Path, chunk.checksum) == "HEALTHY") {
        source = chunk.node1Path;
    } else if (checkChunkStatus(chunk.node2Path, chunk.checksum) == "HEALTHY") {
        source = chunk.node2Path;
    }

    if (source.empty()) {
        std::cout << "RECOVERY FAILED: All three copies are missing or corrupted.\n";
        return;
    }

    std::cout << "Healthy recovery source: " << source << '\n';

    std::vector<std::string> targets = {
        chunk.chunkPath,
        chunk.node1Path,
        chunk.node2Path
    };

    for (const auto& target : targets) {
        if (target == source ||
            checkChunkStatus(target, chunk.checksum) == "HEALTHY") {
            continue;
        }

        std::error_code error;

        fs::create_directories(fs::path(target).parent_path(), error);

        if (error) {
            std::cout << "Recovery failed for " << target
                      << ": " << error.message() << '\n';
            continue;
        }

        fs::copy_file(
            source,
            target,
            fs::copy_options::overwrite_existing,
            error
        );

        if (error) {
            std::cout << "Recovery failed for " << target
                      << ": " << error.message() << '\n';
        } else if (checkChunkStatus(target, chunk.checksum) == "HEALTHY") {
            std::cout << "RECOVERED: " << target << '\n';
        } else {
            std::cout << "RECOVERY VERIFICATION FAILED: "
                      << target << '\n';
        }
    }
}

void verifyExistingChunks() {
    std::ifstream metadataFile("storage/chunk_metadata.txt");

    if (!metadataFile) {
        std::cout << "No saved metadata found. Store a file first.\n";
        return;
    }

    std::vector<StoredChunk> chunks;
    std::string line;

    while (std::getline(metadataFile, line)) {
        std::stringstream record(line);
        std::vector<std::string> fields;
        std::string field;

        while (std::getline(record, field, '|')) {
            fields.push_back(field);
        }
        StoredChunk chunk;

        if (fields.size() == 6) {
            chunk.fileId = fields[0];
            chunk.fileName = fields[1];
            chunk.chunkPath = fields[2];
            chunk.node1Path = fields[3];
            chunk.node2Path = fields[4];
            chunk.checksum = fields[5];
        }
        else if (fields.size() == 4) {
            chunk.chunkPath = fields[0];
            chunk.node1Path = fields[1];
            chunk.node2Path = fields[2];
            chunk.checksum = fields[3];

            fs::path chunkFilePath(chunk.chunkPath);
            fs::path chunksDir = chunkFilePath.parent_path();
            fs::path fileDir = chunksDir.parent_path();

            if (chunksDir.filename() != "chunks" || fileDir.parent_path().filename() != "storage" ||
                fileDir.filename() == "storage") {
                    continue;
            }
            chunk.fileId = fileDir.filename().string();
            chunk.fileName = chunk.fileId;
        }
        else {
            continue;
        }
        fs::path chunkPath(chunk.chunkPath);
        fs::path fileDirectory = chunkPath.parent_path().parent_path();
        if (!fs::exists(fileDirectory) || !fs::is_directory(fileDirectory)) {
            continue;
        }
        chunks.push_back(chunk);
    }

    metadataFile.close();

    if (chunks.empty()) {
        std::cout << "No valid file metadata found.\nStore a file first.\n";
        return;
    }


std::vector<std::pair<std::string, std::string>> files;

for (const auto& entry : fs::directory_iterator("storage")) {
    if (!entry.is_directory()) {
        continue;
    }

    fs::path chunksPath = entry.path() / "chunks";

    if (!fs::exists(chunksPath) || !fs::is_directory(chunksPath)) {
        continue;
    }

    std::string folderName = entry.path().filename().string();
    size_t idPosition = folderName.rfind(" (");

    if (idPosition == std::string::npos ||
        folderName.back() != ')') {
        continue;
    }

    std::string fileName = folderName.substr(0, idPosition);
    std::string fileId = folderName.substr(
        idPosition + 2,
        folderName.size() - idPosition - 3
    );

    files.push_back({fileId, fileName});
}


    std::cout << "\n===== Select File to Verify =====\n";

    for (size_t i = 0; i < files.size(); i++) {
        std::cout << i + 1 << ". " << files[i].second
                  << " (" << files[i].first << ")\n";
    }

    std::cout << "0. Back\n";
    std::cout << "Select file: ";

    size_t selection;

    if (!(std::cin >> selection)) {
        std::cin.clear();
        std::cin.ignore(10000, '\n');
        std::cout << "Invalid selection.\n";
        return;
    }

    std::cin.ignore(10000, '\n');

    if (selection == 0) {
        return;
    }

    if (selection > files.size()) {
        std::cout << "Invalid selection.\n";
        return;
    }

    const auto& selectedFile = files[selection - 1];

    std::cout << "\n===== File Integrity Report =====\n";
    std::cout << "File: " << selectedFile.second << '\n';
    std::cout << "File ID: " << selectedFile.first << '\n';

    int chunkNumber = 0;

    for (const auto& chunk : chunks) {
        if (chunk.fileId != selectedFile.first) {
            continue;
        }

        chunkNumber++;

        std::cout << "\nChunk " << chunkNumber << '\n';
        std::cout << "Main Chunk: "
                  << checkChunkStatus(chunk.chunkPath, chunk.checksum) << '\n';
        std::cout << "Node 1: "
                  << checkChunkStatus(chunk.node1Path, chunk.checksum) << '\n';
        std::cout << "Node 2: "
                  << checkChunkStatus(chunk.node2Path, chunk.checksum) << '\n';
    }

    std::cout << "\nTotal Chunks: " << chunkNumber << '\n';

    std::cout << "\n1. Recover unhealthy chunks\n";
    std::cout << "0. Back\n";
    std::cout << "Choose an option: ";

    int recoveryChoice;

    if (!(std::cin >> recoveryChoice)) {
        std::cin.clear();
        std::cin.ignore(10000, '\n');
        std::cout << "Invalid choice.\n";
        return;
    }

    if (recoveryChoice == 1) {
        for (const auto& chunk : chunks) {
            if (chunk.fileId == selectedFile.first) {
                recoverChunk(chunk);
            }
        }

        std::cout << "\n===== Updated Integrity Report =====\n";

        chunkNumber = 0;

        for (const auto& chunk : chunks) {
            if (chunk.fileId != selectedFile.first) {
                continue;
            }

            chunkNumber++;

            std::cout << "\nChunk " << chunkNumber << '\n';
            std::cout << "Main Chunk: "
                      << checkChunkStatus(chunk.chunkPath, chunk.checksum) << '\n';
            std::cout << "Node 1: "
                      << checkChunkStatus(chunk.node1Path, chunk.checksum) << '\n';
            std::cout << "Node 2: "
                      << checkChunkStatus(chunk.node2Path, chunk.checksum) << '\n';
        }

        std::cout << "\nTotal Chunks: " << chunkNumber << '\n';
        } else if (recoveryChoice != 0) {
        std::cout << "Invalid choice.\n";
        }

}


int main(int argc, char* argv[]) {

     if (argc > 1 && std::string(argv[1]) == "--server") {
        unsigned short port = 8080;
        if (argc > 2) {
            try {
                unsigned long parsedPort = std::stoul(argv[2]);
                if (parsedPort == 0 || parsedPort > 65535) {
                    std::cerr << "Port must be between 1 and 65535.\n";
                    return 1;
                }
                port = static_cast<unsigned short>(parsedPort);
            } catch (...) {
                std::cerr << "Invalid port number.\n";
                return 1;
            }
        }
        return runWebServer(port);
    }

    int choice;

    if (fs::exists("storage") && fs::is_directory("storage")) {
        Database syncDatabase;

        if (syncDatabase.connect()) {
            if (syncDatabase.synchronizeDeletedFiles("storage")) {
                std::cout << "Storage and MySQL synchronization complete.\n";
            } else {
                std::cout << "Warning: Storage synchronization failed.\n";
            }
        } else {
            std::cout << "Warning: MySQL unavailable; synchronization skipped.\n";
        }
    }

    std::cout << "\n===== ChunkVault =====\n";
    std::cout << "1. Store a file\n";
    std::cout << "2. Verify existing chunks\n";
    std::cout << "3. OS Simulator\n";
    std::cout << "Enter choice: ";

    if (!(std::cin >> choice)) {
        std::cin.clear();
        std::cin.ignore(10000, '\n');
        std::cout << "Invalid choice.\n";
        return 1;
    }

    std::cin.ignore(10000, '\n');

    if (choice == 2) {
        verifyExistingChunks();
        return 0;
    }

    if (choice == 3) {
        runOSSimulator();
        return 0;
    }

    if (choice != 1) {
        std::cout << "Invalid choice.\n";
        return 1;
    }

    std::string filePath;

    std::cout << "=================================\n";
    std::cout << "          ChunkVault\n";
    std::cout << "=================================\n";

    std::cout << "Enter file path: ";
    std::getline(std::cin, filePath);

    if (!fs::exists(filePath) || !fs::is_regular_file(filePath)) {
        std::cout << "\nError: Invalid file path.\n";
        return 1;
    }

    fs::path path(filePath);
    uintmax_t fileSize = fs::file_size(path);

    if (fileSize == 0) {
        std::cout << "\nError: File is empty.\n";
        return 1;
    }

    std::string fileId = generateFileId();

    int chunkMode;

    std::cout << "\nChoose chunking mode:\n";
    std::cout << "1. Automatic\n";
    std::cout << "2. Manual number of chunks\n";
    std::cout << "Enter choice: ";
    std::cin >> chunkMode;

    uintmax_t chunkSize;
    uintmax_t totalChunks;

    if (chunkMode == 1) {
        chunkSize = calculateChunkSize(fileSize);
        totalChunks = (fileSize + chunkSize - 1) / chunkSize;
    } 
    else if (chunkMode == 2) {
        std::cout << "Enter number of chunks: ";
        std::cin >> totalChunks;

        if (totalChunks == 0 || totalChunks > fileSize) {
            std::cout << "\nError: Invalid number of chunks.\n";
            return 1;
        }

        chunkSize = (fileSize + totalChunks - 1) / totalChunks;
    } 
    else {
        std::cout << "\nError: Invalid choice.\n";
        return 1;
    }

    std::cout << "\nFile found successfully!\n";
    std::cout << "File ID: " << fileId << '\n';
    std::cout << "File name: " << path.filename() << '\n';
    std::cout << "File size: " << fileSize << " bytes\n";
    std::cout << "Total chunks: " << totalChunks << '\n';
    std::cout << "Chunk size: " << chunkSize << " bytes\n";

    std::ifstream input(filePath, std::ios::binary);

    if (!input) {
        std::cout << "\nError: Unable to open input file.\n";
        return 1;
    }

 
    std::string fileStorage = "storage/" + path.filename().string() + " (" + fileId + ")";

    std::string chunksDirectory = fileStorage + "/chunks";
    std::string node1Directory = fileStorage + "/node1";
    std::string node2Directory = fileStorage + "/node2";

    fs::create_directories(chunksDirectory);
    fs::create_directories(node1Directory);
    fs::create_directories(node2Directory);

    fs::create_directories("storage");
    std::ofstream metadataFile("storage/chunk_metadata.txt", std::ios::app);
    if (!metadataFile) {
        std::cout << "Error: Unable to create metadata file.\n";
        return 1;
    }


    metadataFile.close();

    Database database;

    if (!database.connect()) {
        std::cout << "Error: MySQL connection failed.\n";
        return 1;
    }

    if (!database.insertFile(
        fileId,
        path.filename().string(),
        fileSize,
        static_cast<unsigned int>(totalChunks))) {
            std::cout << "Error: Unable to save file metadata to MySQL.\n";
            return 1;
    }

    std::cout << "File metadata saved to MySQL successfully.\n";

    char buffer[BUFFER_SIZE];

    uintmax_t bytesInChunk = 0;
    int chunkNumber = 1;

    while (input.peek() != EOF) {
        std::string chunkId = generateChunkId();

        ChunkMetadata metadata;
        metadata.fileId = fileId;
        metadata.chunkId = chunkId;
        metadata.chunkNumber = chunkNumber;

        std::string chunkPath =chunksDirectory + "/chunk_" + std::to_string(chunkNumber) + ".chunk";
        std::string node1Path =node1Directory + "/chunk_" + std::to_string(chunkNumber) + ".chunk";
        std::string node2Path =node2Directory + "/chunk_" + std::to_string(chunkNumber) + ".chunk";

        std::ofstream chunkOutput(chunkPath, std::ios::binary);
        std::ofstream node1Output(node1Path, std::ios::binary);
        std::ofstream node2Output(node2Path, std::ios::binary);

        if (!chunkOutput || !node1Output || !node2Output) {
            std::cout << "\nError: Unable to create storage files.\n";
            return 1;
        }

        bytesInChunk = 0;

        while (bytesInChunk < chunkSize && input.peek() != EOF) {
            uintmax_t remaining = chunkSize - bytesInChunk;

            size_t bytesToRead =
                static_cast<size_t>(
                    std::min<uintmax_t>(BUFFER_SIZE, remaining)
                );

            input.read(buffer, bytesToRead);
            std::streamsize bytesRead = input.gcount();

            if (bytesRead <= 0)
                break;

            chunkOutput.write(buffer, bytesRead);
            node1Output.write(buffer, bytesRead);
            node2Output.write(buffer, bytesRead);

            bytesInChunk += bytesRead;
        }
        chunkOutput.close();
        node1Output.close();
        node2Output.close();
        metadata.chunkSize = bytesInChunk;
        metadata.node1Location = node1Path;
        metadata.node2Location = node2Path;
     
        metadata.checksum = calculateChecksum(chunkPath);

        if (!database.insertChunk(
            metadata.chunkId,
            metadata.fileId,
            metadata.chunkNumber,
            metadata.chunkSize,
            metadata.checksum,
            chunkPath,
            node1Path,
            node2Path)) {
                std::cout << "Error: Unable to save chunk metadata to MySQL.\n";
                return 1;
        }

        std::cout << "Chunk metadata saved to MySQL successfully.\n";
        std::ofstream metadataFile("storage/chunk_metadata.txt",std::ios::app);
        if (!metadataFile) {
            std::cout << "Error: Unable to save metadata.\n";
            return 1;
        }
        metadataFile << metadata.fileId << '|'
             << path.filename().string() << '|'
             << chunkPath << '|'
             << node1Path << '|'
             << node2Path << '|'
             << metadata.checksum << '\n';
        metadataFile.close();
        
        std::cout << "\nChunk Metadata:\n";
        std::cout << "File ID: " << metadata.fileId << '\n';
        std::cout << "Chunk ID: " << metadata.chunkId << '\n';
        std::cout << "Chunk Number: " << metadata.chunkNumber << '\n';
        std::cout << "Chunk Size: " << metadata.chunkSize << " bytes\n";
        std::cout << "SHA-256: " << metadata.checksum << '\n';
        std::cout << "Node 1: " << metadata.node1Location << '\n';
        std::cout << "Node 2: " << metadata.node2Location << '\n';
        std::string mainStatus =checkChunkStatus(chunkPath, metadata.checksum);
        std::string node1Status =checkChunkStatus(node1Path, metadata.checksum);
        std::string node2Status =checkChunkStatus(node2Path, metadata.checksum);
        std::cout << "\nIntegrity Status\n";
        std::cout << "Main Chunk: " << mainStatus << '\n';
        std::cout << "Node 1: " << node1Status << '\n';
        std::cout << "Node 2: " << node2Status << '\n';

        if (mainStatus != "HEALTHY" || node1Status != "HEALTHY" || node2Status != "HEALTHY") {
            std::cout << "WARNING: Integrity issue detected!\n";
        }

        chunkNumber++;
    }
    std::cout << "\nFile chunking and replication completed successfully!\n";
    return 0;
}
