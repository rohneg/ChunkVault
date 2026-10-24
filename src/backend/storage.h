#ifndef CHUNKVAULT_STORAGE_H
#define CHUNKVAULT_STORAGE_H

#include "../../database/Database.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace storage {

struct StoreResult {
    std::string id;
    std::uintmax_t size = 0;
    std::string mimeType;
    std::string sha256;
    std::vector<ChunkRecord> chunks;
    bool logged = false;
};

struct RestoreResult {
    std::string sha256;
    unsigned repaired = 0;
};

std::string randomId(const std::string& prefix, int length);
std::string checksum(const std::filesystem::path& path);
std::string detectMime(const std::filesystem::path& path, const std::string& name);
std::string status(const std::string& path, const std::string& expectedChecksum);

inline std::array<std::string, 3> locations(const ChunkRecord& chunk) {
    return {chunk.mainLocation, chunk.node1Location, chunk.node2Location};
}
std::array<std::string, 3> statuses(const ChunkRecord& chunk);

unsigned recover(const ChunkRecord& chunk);

StoreResult storeFile(Database& database, const std::filesystem::path& source,
                      const std::string& name, std::uintmax_t chunkCount = 0);

RestoreResult reconstruct(const std::vector<ChunkRecord>& chunks,
                          const std::function<bool(const char*, std::size_t)>& sink);

}

#endif