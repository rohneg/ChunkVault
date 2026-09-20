#include <iostream>
#include <filesystem>
#include <string>
#include <fstream>
#include <cmath>
#include <algorithm>

namespace fs = std::filesystem;

const uintmax_t BASE_SIZE = 1024ULL * 1024ULL;
const size_t BUFFER_SIZE = 4ULL * 1024ULL * 1024ULL;

uintmax_t calculateChunkSize(uintmax_t fileSize) {
    double chunkSize = std::sqrt(
        static_cast<double>(fileSize) *
        static_cast<double>(BASE_SIZE)
    );

    return static_cast<uintmax_t>(chunkSize);
}

int main() {
    std::string filePath;

    std::cout << "=================================\n";
    std::cout << "          ChunkVault\n";
    std::cout << "=================================\n";

    std::cout << "Enter file path: ";
    std::getline(std::cin, filePath);

    if (!fs::exists(filePath)) {
        std::cout << "\nError: File not found.\n";
        return 1;
    }

    if (!fs::is_regular_file(filePath)) {
        std::cout << "\nError: Selected path is not a file.\n";
        return 1;
    }

    fs::path path(filePath);
    uintmax_t fileSize = fs::file_size(path);

    if (fileSize == 0) {
        std::cout << "\nError: File is empty.\n";
        return 1;
    }

    uintmax_t chunkSize = calculateChunkSize(fileSize);

    std::cout << "\nFile found successfully!\n";
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

    while (input) {
        if (!chunkOutput.is_open()) {
            std::string chunkPath =
                "storage/chunks/chunk_" +
                std::to_string(chunkNumber) +
                ".chunk";

            std::string node1Path =
                "storage/node1/chunk_" +
                std::to_string(chunkNumber) +
                ".chunk";

            std::string node2Path =
                "storage/node2/chunk_" +
                std::to_string(chunkNumber) +
                ".chunk";

            chunkOutput.open(chunkPath, std::ios::binary);
            node1Output.open(node1Path, std::ios::binary);
            node2Output.open(node2Path, std::ios::binary);

            if (!chunkOutput || !node1Output || !node2Output) {
                std::cout << "\nError: Unable to create storage files.\n";
                return 1;
            }
        }

        uintmax_t remaining = chunkSize - bytesInChunk;

        size_t bytesToRead =
            static_cast<size_t>(
                std::min<uintmax_t>(BUFFER_SIZE, remaining)
            );

        input.read(buffer, bytesToRead);
        std::streamsize bytesRead = input.gcount();

        if (bytesRead <= 0) {
            break;
        }

        chunkOutput.write(buffer, bytesRead);
        node1Output.write(buffer, bytesRead);
        node2Output.write(buffer, bytesRead);

        bytesInChunk += static_cast<uintmax_t>(bytesRead);

        if (bytesInChunk >= chunkSize) {
            chunkOutput.close();
            node1Output.close();
            node2Output.close();

            std::cout << "Chunk "
                      << chunkNumber
                      << " replicated to Node 1 and Node 2 ("
                      << bytesInChunk
                      << " bytes)\n";

            chunkNumber++;
            bytesInChunk = 0;
        }
    }

    if (chunkOutput.is_open()) {
        chunkOutput.close();
        node1Output.close();
        node2Output.close();

        std::cout << "Chunk "
                  << chunkNumber
                  << " replicated to Node 1 and Node 2 ("
                  << bytesInChunk
                  << " bytes)\n";
    }

    input.close();

    std::cout << "\nFile chunking and replication completed successfully!\n";

    return 0;
}