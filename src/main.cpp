#include <iostream>
#include <filesystem>
#include <string>
#include <cmath>

namespace fs = std::filesystem;

const uintmax_t BASE_SIZE = 1024ULL * 1024ULL; // 1 MB

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

    std::cout << "\nFile found successfully!\n";
    std::cout << "File name: " << path.filename() << '\n';
    std::cout << "File size: " << fileSize << " bytes\n";

    uintmax_t chunkSize = calculateChunkSize(fileSize);

    std::cout << "Calculated chunk size: "
              << chunkSize
              << " bytes\n";

    return 0;
}