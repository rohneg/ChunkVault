#include <iostream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

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

    std::cout << "\nFile found successfully!\n";
    std::cout << "File name: " << path.filename() << '\n';
    std::cout << "File size: " << fs::file_size(path) << " bytes\n";

    return 0;
}