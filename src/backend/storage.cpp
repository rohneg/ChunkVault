#include "storage.h"

#include <CommonCrypto/CommonDigest.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>

namespace fs = std::filesystem;

namespace storage {

namespace {

std::uintmax_t autoChunkSize(std::uintmax_t fileSize) {
    const std::uintmax_t minimum = 64ULL * 1024, maximum = 10ULL * 1024 * 1024 * 1024;
    const double baseChunk = 256.0 * 1024 * 1024, baseFile = 1024.0 * 1024 * 1024;
    auto size = static_cast<std::uintmax_t>(baseChunk * std::pow(static_cast<double>(fileSize) / baseFile, 0.65));
    return std::min({std::max(size, minimum), maximum, fileSize});
}

}

std::string randomId(const std::string& prefix, int length) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static std::random_device random;
    std::string id = prefix;
    for (int i = 0; i < length; ++i) id += alphabet[random() % (sizeof(alphabet) - 1)];
    return id;
}

std::string checksum(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return "";

    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    std::array<char, 65536> buffer;
    while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
        CC_SHA256_Update(&context, buffer.data(), static_cast<CC_LONG>(input.gcount()));
    }
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &context);

    static const char hex[] = "0123456789abcdef";
    std::string out;
    for (unsigned char byte : digest) {
        out += hex[byte >> 4];
        out += hex[byte & 15];
    }
    return out;
}

std::string status(const std::string& path, const std::string& expectedChecksum) {
    std::error_code ignored;
    if (path.empty() || !fs::is_regular_file(path, ignored)) return "MISSING";
    const std::string actual = checksum(path);
    if (actual.empty()) return "ERROR";
    return actual == expectedChecksum ? "HEALTHY" : "CORRUPTED";
}

std::array<std::string, 3> statuses(const ChunkRecord& chunk) {
    const auto paths = locations(chunk);
    return {status(paths[0], chunk.checksum), status(paths[1], chunk.checksum), status(paths[2], chunk.checksum)};
}

unsigned recover(const ChunkRecord& chunk) {
    const auto paths = locations(chunk);
    const auto states = statuses(chunk);
    const auto healthy = std::find(states.begin(), states.end(), "HEALTHY");
    if (healthy == states.end()) return 0;
    const std::string& source = paths[static_cast<size_t>(healthy - states.begin())];

    unsigned restored = 0;
    for (size_t i = 0; i < paths.size(); ++i) {
        if (states[i] == "HEALTHY") continue;
        std::error_code error;
        fs::create_directories(fs::path(paths[i]).parent_path(), error);
        if (!error) fs::copy_file(source, paths[i], fs::copy_options::overwrite_existing, error);
        if (!error && status(paths[i], chunk.checksum) == "HEALTHY") ++restored;
    }
    return restored;
}

StoreResult storeFile(Database& database, const fs::path& source, const std::string& name,
                      std::uintmax_t chunkCount) {
    const std::uintmax_t size = fs::file_size(source);
    if (size == 0) throw std::invalid_argument("The selected file is empty.");
    if (chunkCount > std::min<std::uintmax_t>(size, std::numeric_limits<unsigned int>::max())) {
        throw std::invalid_argument("The number of chunks must be between 1 and the file size in bytes.");
    }
    const std::uintmax_t chunkSize = chunkCount ? (size + chunkCount - 1) / chunkCount : autoChunkSize(size);
    const std::uintmax_t total = (size + chunkSize - 1) / chunkSize;

    StoreResult result;
    result.size = size;
    fs::path directory;
    bool inserted = false;
    for (int attempt = 0; attempt < 20 && !inserted; ++attempt) {
        result.id = randomId("FILE_", 4);
        directory = fs::path("storage") / (name + " (" + result.id + ")");
        inserted = !fs::exists(directory) &&
                   database.insertFile(result.id, name, size, static_cast<unsigned int>(total));
    }
    if (!inserted) throw std::runtime_error("MySQL did not save the file metadata; no storage job was started.");

    for (const char* folder : {"chunks", "node1", "node2"}) fs::create_directories(directory / folder);

    std::ifstream input(source, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to reopen the uploaded file.");
    std::vector<char> buffer(4ULL * 1024 * 1024);

    std::uintmax_t done = 0;
    for (std::uintmax_t n = 1; n <= total; ++n) {
        ChunkRecord chunk;
        chunk.chunkId = randomId("CHUNK_", 8);
        chunk.fileId = result.id;
        chunk.chunkNumber = static_cast<unsigned int>(n);
        chunk.chunkSize = std::min(chunkSize, size - done);

        const std::string fileName = "chunk_" + std::to_string(n) + ".chunk";
        chunk.mainLocation = (directory / "chunks" / fileName).string();
        chunk.node1Location = (directory / "node1" / fileName).string();
        chunk.node2Location = (directory / "node2" / fileName).string();

        std::ofstream outputs[3] = {std::ofstream(chunk.mainLocation, std::ios::binary),
                                    std::ofstream(chunk.node1Location, std::ios::binary),
                                    std::ofstream(chunk.node2Location, std::ios::binary)};
        for (const auto& out : outputs) {
            if (!out) throw std::runtime_error("Unable to create a chunk or replica file.");
        }

        for (std::uintmax_t left = chunk.chunkSize; left > 0;) {
            input.read(buffer.data(), static_cast<std::streamsize>(std::min<std::uintmax_t>(buffer.size(), left)));
            const std::streamsize got = input.gcount();
            if (got <= 0) throw std::runtime_error("The uploaded file could not be read completely.");
            for (auto& out : outputs) out.write(buffer.data(), got);
            left -= static_cast<std::uintmax_t>(got);
        }
        for (auto& out : outputs) {
            out.close();
            if (!out) throw std::runtime_error("Unable to write a chunk replica.");
        }

        chunk.checksum = checksum(chunk.mainLocation);
        if (chunk.checksum.empty()) throw std::runtime_error("Unable to calculate the chunk checksum.");
        if (!database.insertChunk(chunk.chunkId, chunk.fileId, chunk.chunkNumber, chunk.chunkSize, chunk.checksum,
                                  chunk.mainLocation, chunk.node1Location, chunk.node2Location)) {
            throw std::runtime_error("MySQL did not save chunk metadata.");
        }
        done += chunk.chunkSize;
        result.chunks.push_back(std::move(chunk));
    }

    result.logged = database.logActivity("STORAGE", name + " (" + result.id + ") stored with " +
                                                     std::to_string(total) + " chunk(s).");
    return result;
}

}