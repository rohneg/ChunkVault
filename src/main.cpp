#include <iostream>
#include <filesystem>
#include <string>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <random>

namespace fs = std::filesystem;

const uintmax_t BASE_SIZE = 1024ULL * 1024ULL;
const size_t BUFFER_SIZE = 4ULL * 1024ULL * 1024ULL;

uintmax_t calculateChunkSize(uintmax_t fileSize) {
    const uintmax_t MIN_CHUNK = 64ULL * 1024ULL;
    const uintmax_t MAX_CHUNK = 256ULL * 1024ULL * 1024ULL;

    uintmax_t chunkSize = static_cast<uintmax_t>(
        std::sqrt(static_cast<double>(fileSize) * BASE_SIZE)
    );

    chunkSize = std::max(chunkSize, MIN_CHUNK);
    chunkSize = std::min(chunkSize, MAX_CHUNK);
    chunkSize = std::min(chunkSize, fileSize);

    return chunkSize;
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
};

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

    uintmax_t chunkSize = calculateChunkSize(fileSize);

    std::cout << "\nFile found successfully!\n";
    std::cout << "File ID: " << fileId << '\n';
    std::cout << "File name: " << path.filename() << '\n';
    std::cout << "File size: " << fileSize << " bytes\n";
    std::cout << "Calculated chunk size: " << chunkSize << " bytes\n";

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

    std::ofstream chunkOutput;
    std::ofstream node1Output;
    std::ofstream node2Output;

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

        chunkOutput.open(chunkPath, std::ios::binary);
        node1Output.open(node1Path, std::ios::binary);
        node2Output.open(node2Path, std::ios::binary);

        if (!chunkOutput || !node1Output || !node2Output) {
            std::cout << "\nError: Unable to create storage files.\n";
            return 1;
        }

        bytesInChunk = 0;

        while (bytesInChunk < chunkSize && input.peek() != EOF) {
            uintmax_t remaining = chunkSize - bytesInChunk;

            size_t bytesToRead =
                static_cast<size_t>(std::min<uintmax_t>(BUFFER_SIZE, remaining));

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

        std::cout << "\nChunk Metadata\n";
        std::cout << "File ID: " << metadata.fileId << '\n';
        std::cout << "Chunk ID: " << metadata.chunkId << '\n';
        std::cout << "Chunk Number: " << metadata.chunkNumber << '\n';
        std::cout << "Chunk Size: " << metadata.chunkSize << " bytes\n";
        std::cout << "Node 1: " << metadata.node1Location << '\n';
        std::cout << "Node 2: " << metadata.node2Location << '\n';

        chunkNumber++;
    }

    std::cout << "\nFile chunking and replication completed successfully!\n";

    return 0;
}