#include <iostream>
#include <filesystem>
#include <string>
#include <fstream>
#include <cmath>

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

    char buffer[BUFFER_SIZE];
    uintmax_t bytesInChunk = 0;
    int chunkNumber = 1;

    std::ofstream output;

    while (input) {
        if (!output.is_open()) {
            std::string chunkPath =
                "storage/chunks/chunk_" +
                std::to_string(chunkNumber) +
                ".chunk";

            output.open(chunkPath, std::ios::binary);

            if (!output) {
                std::cout << "\nError: Unable to create chunk file.\n";
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

        output.write(buffer, bytesRead);
        bytesInChunk += static_cast<uintmax_t>(bytesRead);

        if (bytesInChunk >= chunkSize) {
            output.close();

            std::cout << "Created chunk "
                      << chunkNumber
                      << " (" << bytesInChunk << " bytes)\n";

            chunkNumber++;
            bytesInChunk = 0;
        }
    }

    if (output.is_open()) {
        output.close();

        std::cout << "Created chunk "
                  << chunkNumber
                  << " (" << bytesInChunk << " bytes)\n";
    }

    input.close();

    std::cout << "\nFile chunking completed successfully!\n";

    return 0;
}