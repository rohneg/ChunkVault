#include "storage.h"

#include <CommonCrypto/CommonDigest.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <random>
#include <stdexcept>
#include <unordered_map>

namespace fs = std::filesystem;

namespace storage {

namespace {

std::uintmax_t autoChunkSize(std::uintmax_t fileSize) {
    const std::uintmax_t minimum = 64ULL * 1024, maximum = 10ULL * 1024 * 1024 * 1024;
    const double baseChunk = 256.0 * 1024 * 1024, baseFile = 1024.0 * 1024 * 1024;
    auto size = static_cast<std::uintmax_t>(baseChunk * std::pow(static_cast<double>(fileSize) / baseFile, 0.65));
    return std::min({std::max(size, minimum), maximum, fileSize});
}

bool healthyCached(const std::string& path, const std::string& expectedChecksum) {
    static std::mutex lock;
    static std::unordered_map<std::string, std::pair<std::uintmax_t, fs::file_time_type>> verified;

    std::error_code error;
    const auto size = fs::file_size(path, error);
    const auto time = fs::last_write_time(path, error);
    if (error) return false;
    const std::string key = path + '|' + expectedChecksum;
    {
        std::lock_guard<std::mutex> guard(lock);
        const auto found = verified.find(key);
        if (found != verified.end() && found->second.first == size && found->second.second == time) return true;
    }
    if (status(path, expectedChecksum) != "HEALTHY") return false;
    std::lock_guard<std::mutex> guard(lock);
    verified[key] = {size, time};
    return true;
}

std::string toHex(const unsigned char* bytes, std::size_t count) {
    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        out += hex[bytes[i] >> 4];
        out += hex[bytes[i] & 15];
    }
    return out;
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
    return toHex(digest, CC_SHA256_DIGEST_LENGTH);
}

std::string detectMime(const fs::path& path, const std::string& name) {
    std::ifstream input(path, std::ios::binary);
    char head[512];
    input.read(head, sizeof(head));
    const size_t size = static_cast<size_t>(input.gcount());

    auto at = [&](size_t offset, const std::string& magic) {
        return size >= offset + magic.size() && std::memcmp(head + offset, magic.data(), magic.size()) == 0;
    };
    std::string ext = fs::path(name).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (at(0, "\x89PNG")) return "image/png";
    if (at(0, "\xFF\xD8\xFF")) return "image/jpeg";
    if (at(0, "GIF8")) return "image/gif";
    if (at(0, "RIFF") && at(8, "WEBP")) return "image/webp";
    if (at(0, "RIFF") && at(8, "WAVE")) return "audio/wav";
    if (at(0, "%PDF")) return "application/pdf";
    if (at(0, "OggS")) return "audio/ogg";
    if (at(0, "fLaC")) return "audio/flac";
    if (at(0, "ID3")) return "audio/mpeg";
    if (at(0, "\x1A\x45\xDF\xA3")) return ext == ".mkv" ? "video/x-matroska" : "video/webm";
    if (at(4, "ftyp")) {
        if (at(8, "qt  ")) return "video/quicktime";
        if (at(8, "M4A ")) return "audio/mp4";
        return "video/mp4";
    }
    if (at(0, "\x1f\x8b")) return "application/gzip";
    if (at(0, "Rar!")) return "application/vnd.rar";
    if (at(0, "7z\xBC\xAF")) return "application/x-7z-compressed";
    if (at(0, "SQLite format 3")) return "application/vnd.sqlite3";
    if (at(0, "BM") && ext == ".bmp") return "image/bmp";
    if (ext == ".mp3" && size >= 2 && static_cast<unsigned char>(head[0]) == 0xFF &&
        (static_cast<unsigned char>(head[1]) & 0xE0) == 0xE0) return "audio/mpeg";
    if (at(0, "PK\x03\x04")) {
        static const std::unordered_map<std::string, const char*> office = {
            {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
            {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
            {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"}};
        const auto found = office.find(ext);
        return found == office.end() ? "application/zip" : found->second;
    }

    size_t control = 0;
    bool text = size > 0;
    for (size_t i = 0; i < size && text; ++i) {
        const auto byte = static_cast<unsigned char>(head[i]);
        if (byte == 0) text = false;
        else if (byte < 9 || (byte > 13 && byte < 32)) ++control;
    }
    if (!text || control * 50 > size) return "application/octet-stream";

    static const std::unordered_map<std::string, const char*> textTypes = {
        {".json", "application/json"}, {".xml", "application/xml"}, {".html", "text/html"}, {".htm", "text/html"},
        {".css", "text/css"}, {".js", "text/javascript"}, {".csv", "text/csv"}, {".md", "text/markdown"},
        {".svg", "image/svg+xml"}};
    const auto found = textTypes.find(ext);
    return found == textTypes.end() ? "text/plain" : found->second;
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
    result.sha256 = checksum(source);
    if (result.sha256.empty()) throw std::runtime_error("Unable to read the uploaded file.");
    result.mimeType = detectMime(source, name);

    fs::path directory;
    bool inserted = false;
    for (int attempt = 0; attempt < 20 && !inserted; ++attempt) {
        result.id = randomId("FILE_", 4);
        directory = fs::path("storage") / (name + " (" + result.id + ")");
        inserted = !fs::exists(directory) &&
                   database.insertFile(result.id, name, size, static_cast<unsigned int>(total),
                                       result.mimeType, result.sha256);
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

RestoreResult reconstruct(const std::vector<ChunkRecord>& chunks,
                          const std::function<bool(const char*, std::size_t)>& sink,
                          std::uint64_t offset, std::uint64_t length) {
    constexpr std::uint64_t kMax = std::numeric_limits<std::uint64_t>::max();
    const bool partial = offset != 0 || length != kMax;
    const std::uint64_t end = length > kMax - offset ? kMax : offset + length;

    RestoreResult result;
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    std::vector<char> buffer(1 << 20);

    std::uint64_t position = 0;
    for (const auto& chunk : chunks) {
        const std::uint64_t chunkStart = position, chunkEnd = position + chunk.chunkSize;
        position = chunkEnd;
        if (chunkEnd <= offset) continue;
        if (chunkStart >= end) break;

        const auto paths = locations(chunk);
        int source = -1;
        for (size_t i = 0; i < paths.size() && source < 0; ++i) {
            const bool healthy = partial ? healthyCached(paths[i], chunk.checksum)
                                         : status(paths[i], chunk.checksum) == "HEALTHY";
            if (healthy) source = static_cast<int>(i);
        }
        if (source < 0) {
            throw std::runtime_error("Chunk " + std::to_string(chunk.chunkNumber) +
                                     " has no healthy copy. Run Replicate first.");
        }
        if (source > 0) result.repaired += recover(chunk);

        std::ifstream input(paths[static_cast<size_t>(source)], std::ios::binary);
        if (!input) throw std::runtime_error("Unable to read chunk " + std::to_string(chunk.chunkNumber) + ".");
        const std::uint64_t from = std::max(chunkStart, offset), to = std::min(chunkEnd, end);
        input.seekg(static_cast<std::streamoff>(from - chunkStart));
        for (std::uint64_t left = to - from; left > 0;) {
            input.read(buffer.data(), static_cast<std::streamsize>(std::min<std::uint64_t>(buffer.size(), left)));
            const auto got = static_cast<std::size_t>(input.gcount());
            if (got == 0) throw std::runtime_error("Unable to read chunk " + std::to_string(chunk.chunkNumber) + ".");
            CC_SHA256_Update(&context, buffer.data(), static_cast<CC_LONG>(got));
            if (!sink(buffer.data(), got)) throw std::runtime_error("The output was closed before the file was complete.");
            left -= got;
        }
    }

    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &context);
    result.sha256 = toHex(digest, CC_SHA256_DIGEST_LENGTH);
    return result;
}

}