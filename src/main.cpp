#include "../database/Database.h"
#include "backend/os_simulator.h"
#include "backend/storage.h"
#include "backend/web_server.h"
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

void discardLine() {
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

void printChunkReport(const std::vector<ChunkRecord>& chunks) {
    for (const auto& chunk : chunks) {
        const auto states = storage::statuses(chunk);
        std::cout << "\nChunk " << chunk.chunkNumber << "\nMain Chunk: " << states[0]
                  << "\nNode 1: " << states[1] << "\nNode 2: " << states[2] << '\n';
    }
    std::cout << "\nTotal Chunks: " << chunks.size() << '\n';
}

int storeFile() {
    std::string filePath;
    std::cout << "Enter file path: ";
    std::getline(std::cin, filePath);
    std::error_code ignored;
    if (!fs::is_regular_file(filePath, ignored)) {
        std::cout << "\nError: Invalid file path.\n";
        return 1;
    }

    int mode = 0;
    std::cout << "\nChoose chunking mode:\n1. Automatic\n2. Manual number of chunks\nEnter choice: ";
    std::cin >> mode;
    std::uintmax_t chunkCount = 0;
    if (mode == 2) {
        std::cout << "Enter number of chunks: ";
        if (!(std::cin >> chunkCount) || chunkCount == 0) {
            std::cout << "\nError: Invalid number of chunks.\n";
            return 1;
        }
    } else if (mode != 1) {
        std::cout << "\nError: Invalid choice.\n";
        return 1;
    }

    Database database;
    if (!database.connect()) {
        std::cout << "Error: MySQL connection failed.\n";
        return 1;
    }
    try {
        const auto result = storage::storeFile(database, filePath, fs::path(filePath).filename().string(), chunkCount);
        std::cout << "\nFile ID: " << result.id << "\nFile size: " << result.size << " bytes\n";
        printChunkReport(result.chunks);
        std::cout << "\nFile chunking and replication completed successfully!\n";
    } catch (const std::exception& error) {
        std::cout << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

int verifyFiles() {
    Database database;
    std::vector<FileRecord> files;
    if (!database.connect() || !database.getFiles(files)) {
        std::cout << "Error: Unable to read files from MySQL.\n";
        return 1;
    }
    if (files.empty()) {
        std::cout << "No stored files found. Store a file first.\n";
        return 0;
    }

    std::cout << "\n===== Select File to Verify =====\n";
    for (size_t i = 0; i < files.size(); ++i) std::cout << i + 1 << ". " << files[i].fileName << " (" << files[i].fileId << ")\n";
    std::cout << "0. Back\nSelect file: ";

    size_t selection = 0;
    if (!(std::cin >> selection) || selection > files.size()) {
        discardLine();
        std::cout << "Invalid selection.\n";
        return 0;
    }
    if (selection == 0) return 0;

    const FileRecord& file = files[selection - 1];
    std::vector<ChunkRecord> chunks;
    if (!database.getChunks(chunks, file.fileId)) {
        std::cout << "Error: Unable to read chunk records from MySQL.\n";
        return 1;
    }
    std::cout << "\n===== File Integrity Report =====\nFile: " << file.fileName << "\nFile ID: " << file.fileId << '\n';
    printChunkReport(chunks);

    std::cout << "\n1. Recover unhealthy chunks\n0. Back\nChoose an option: ";
    int choice = -1;
    if (!(std::cin >> choice)) discardLine();
    if (choice == 1) {
        unsigned restored = 0;
        for (const auto& chunk : chunks) restored += storage::recover(chunk);
        std::cout << "\nRestored " << restored << " copy/copies.\n\n===== Updated Integrity Report =====\n";
        printChunkReport(chunks);
    } else if (choice != 0) {
        std::cout << "Invalid choice.\n";
    }
    return 0;
}

void syncStorage() {
    if (!fs::is_directory("storage")) return;
    Database database;
    if (!database.connect()) std::cout << "Warning: MySQL unavailable; synchronization skipped.\n";
    else std::cout << (database.synchronizeDeletedFiles("storage") ? "Storage and MySQL synchronization complete.\n"
                                                                    : "Warning: Storage synchronization failed.\n");
}

}

int main(int argc, char* argv[]) {
    if (argc > 1 && std::string(argv[1]) == "--server") {
        unsigned short port = 8080;
        if (argc > 2) {
            try {
                const unsigned long value = std::stoul(argv[2]);
                if (value == 0 || value > 65535) throw std::out_of_range("port");
                port = static_cast<unsigned short>(value);
            } catch (...) {
                std::cerr << "Port must be a number between 1 and 65535.\n";
                return 1;
            }
        }
        return runWebServer(port);
    }

    syncStorage();
    std::cout << "\n===== ChunkVault =====\n1. Store a file\n2. Verify existing chunks\n3. OS Simulator\nEnter choice: ";
    int choice = 0;
    if (!(std::cin >> choice)) {
        discardLine();
        std::cout << "Invalid choice.\n";
        return 1;
    }
    discardLine();

    switch (choice) {
        case 1: return storeFile();
        case 2: return verifyFiles();
        case 3: runOSSimulator(); return 0;
        default: std::cout << "Invalid choice.\n"; return 1;
    }
}