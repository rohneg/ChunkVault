#include <iostream>
#include <filesystem>
#include <string>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <random>
#include <CommonCrypto/CommonDigest.h>
#include <cstdio>


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

int main() {
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

    fs::create_directories("storage/chunks");
    fs::create_directories("storage/node1");
    fs::create_directories("storage/node2");

    char buffer[BUFFER_SIZE];
    uintmax_t bytesInChunk = 0;
    int chunkNumber = 1;

    while (input.peek() != EOF) {
        std::string chunkId = generateChunkId();

        ChunkMetadata metadata;
        metadata.fileId = fileId;
        metadata.chunkId = chunkId;
        metadata.chunkNumber = chunkNumber;

        std::string chunkPath =
            "storage/chunks/chunk_" + std::to_string(chunkNumber) + ".chunk";

        std::string node1Path =
            "storage/node1/chunk_" + std::to_string(chunkNumber) + ".chunk";

        std::string node2Path =
            "storage/node2/chunk_" + std::to_string(chunkNumber) + ".chunk";

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

        if (metadata.checksum.empty()) {
            std::cerr << "Error: Failed to calculate checksum for chunk "
            << metadata.chunkId << '\n';
        return 1;
        }

        metadata.checksum = calculateChecksum(chunkPath);

        if (metadata.checksum.empty()) {
         std::cerr << "Error: Failed to calculate checksum for chunk "
              << metadata.chunkId << '\n';
                 return 1;
      }

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
