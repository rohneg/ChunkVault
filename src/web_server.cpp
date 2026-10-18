#include "web_server.h"
#include "os_simulator.h"
#include "../database/Database.h"

#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

std::string allowedOrigin;

struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    std::string body;
    std::unordered_map<std::string, std::string> headers;
    unsigned long long contentLength = 0;
};

struct CopyStatus {
    std::string mainStatus;
    std::string node1Status;
    std::string node2Status;
};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(const std::string& value) {
    size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    size_t end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string jsonEscape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (unsigned char c : value) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (c < 0x20) {
                    char encoded[7];
                    std::snprintf(encoded, sizeof(encoded), "\\u%04x", c);
                    result += encoded;
                } else {
                    result += static_cast<char>(c);
                }
        }
    }
    return result;
}

std::string urlDecode(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+' ) {
            result += ' ';
        } else if (value[i] == '%' && i + 2 < value.size()) {
            char encoded[3] = {value[i + 1], value[i + 2], '\0'};
            char* end = nullptr;
            long decoded = std::strtol(encoded, &end, 16);
            if (end != encoded + 2) return "";
            result += static_cast<char>(decoded);
            i += 2;
        } else {
            result += value[i];
        }
    }
    return result;
}

std::unordered_map<std::string, std::string> parseQuery(const std::string& query) {
    std::unordered_map<std::string, std::string> values;
    size_t start = 0;
    while (start <= query.size()) {
        size_t end = query.find('&', start);
        std::string item = query.substr(start, end == std::string::npos ? end : end - start);
        size_t equal = item.find('=');
        std::string key = urlDecode(item.substr(0, equal));
        std::string value = equal == std::string::npos ? "" : urlDecode(item.substr(equal + 1));
        if (!key.empty()) values[key] = value;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return values;
}

int formInteger(const std::unordered_map<std::string, std::string>& values,
                const std::string& key, int minimum, int maximum) {
    auto found = values.find(key);
    if (found == values.end() || found->second.empty()) {
        throw std::invalid_argument("The OS simulation request is missing " + key + ".");
    }
    size_t parsedLength = 0;
    long long value;
    try {
        value = std::stoll(found->second, &parsedLength);
    } catch (...) {
        throw std::invalid_argument("The OS simulation value for " + key + " is invalid.");
    }
    if (parsedLength != found->second.size() || value < minimum || value > maximum) {
        throw std::invalid_argument("The OS simulation value for " + key + " is outside its allowed range.");
    }
    return static_cast<int>(value);
}

std::vector<Process> schedulingProcesses(const std::unordered_map<std::string, std::string>& values) {
    int count = formInteger(values, "count", 1, 256);
    std::vector<Process> processes;
    processes.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index) {
        std::string suffix = std::to_string(index);
        Process process;
        process.pid = formInteger(values, "pid" + suffix, 1, std::numeric_limits<int>::max());
        process.arrivalTime = formInteger(values, "arrival" + suffix, 0, std::numeric_limits<int>::max());
        process.burstTime = formInteger(values, "burst" + suffix, 1, std::numeric_limits<int>::max());
        auto name = values.find("name" + suffix);
        process.name = name == values.end() || name->second.empty() ? "P" + std::to_string(process.pid) : name->second;
        if (process.name.size() > 255) {
            throw std::invalid_argument("Process names must be 255 characters or fewer.");
        }
        processes.push_back(std::move(process));
    }
    return processes;
}

std::string scheduleJson(const ScheduleResult& result) {
    std::ostringstream json;
    json << std::fixed << std::setprecision(4);
    json << "{\"algorithm\":\"" << jsonEscape(result.algorithm)
         << "\",\"totalTime\":" << result.totalTime
         << ",\"averageWaiting\":" << result.averageWaiting
         << ",\"averageTurnaround\":" << result.averageTurnaround
         << ",\"cpuUtilization\":" << result.cpuUtilization
         << ",\"throughput\":" << result.throughput << ",\"gantt\":[";
    for (size_t index = 0; index < result.gantt.size(); ++index) {
        if (index) json << ',';
        const auto& segment = result.gantt[index];
        json << "{\"pid\":" << segment.pid
             << ",\"idle\":" << (segment.idle ? "true" : "false")
             << ",\"start\":" << segment.start
             << ",\"end\":" << segment.end << '}';
    }
    json << "],\"results\":[";
    for (size_t index = 0; index < result.processes.size(); ++index) {
        if (index) json << ',';
        const auto& process = result.processes[index];
        json << "{\"pid\":" << process.pid
             << ",\"name\":\"" << jsonEscape(process.name)
             << "\",\"arrivalTime\":" << process.arrivalTime
             << ",\"burstTime\":" << process.burstTime
             << ",\"completionTime\":" << process.completionTime
             << ",\"waitingTime\":" << process.waitingTime
             << ",\"turnaroundTime\":" << process.turnaroundTime << '}';
    }
    json << "]}";
    return json.str();
}

struct ResourceGraphInput {
    std::vector<int> processIds;
    std::vector<OSResource> resources;
    std::vector<OSResourceRequest> requests;
};

ResourceGraphInput resourceGraphFromForm(const std::unordered_map<std::string, std::string>& values) {
    int processCount = formInteger(values, "processCount", 0, 256);
    ResourceGraphInput graph;
    std::unordered_set<int> knownProcesses;
    for (int index = 0; index < processCount; ++index) {
        int pid = formInteger(values, "processPid" + std::to_string(index), 1, std::numeric_limits<int>::max());
        if (!knownProcesses.insert(pid).second) throw std::invalid_argument("Process IDs must be unique.");
        graph.processIds.push_back(pid);
    }

    int resourceCount = formInteger(values, "resourceCount", 0, 256);
    std::unordered_set<int> knownResources;
    for (int index = 0; index < resourceCount; ++index) {
        int id = formInteger(values, "resourceId" + std::to_string(index), 1, std::numeric_limits<int>::max());
        int owner = formInteger(values, "resourceOwner" + std::to_string(index), 0, std::numeric_limits<int>::max());
        if (!knownResources.insert(id).second) throw std::invalid_argument("Resource IDs must be unique.");
        if (owner != 0 && !knownProcesses.count(owner)) throw std::invalid_argument("A resource owner is not in the process list.");
        graph.resources.push_back({id, owner});
    }

    int requestCount = formInteger(values, "requestCount", 0, 1024);
    graph.requests.reserve(static_cast<size_t>(requestCount));
    for (int index = 0; index < requestCount; ++index) {
        int pid = formInteger(values, "requestPid" + std::to_string(index), 1, std::numeric_limits<int>::max());
        int resourceId = formInteger(values, "requestResource" + std::to_string(index), 1, std::numeric_limits<int>::max());
        if (!knownProcesses.count(pid) || !knownResources.count(resourceId)) {
            throw std::invalid_argument("A resource request refers to an unknown process or resource.");
        }
        graph.requests.push_back({pid, resourceId});
    }
    return graph;
}

std::vector<int> deadlockCycleFromForm(const std::unordered_map<std::string, std::string>& values) {
    ResourceGraphInput graph = resourceGraphFromForm(values);
    return detectDeadlockCycle(graph.processIds, graph.resources, graph.requests);
}

std::string resourceActionJson(const ResourceActionResult& result) {
    std::ostringstream json;
    json << "{\"changedResourceId\":" << result.changedResourceId
         << ",\"message\":\"" << jsonEscape(result.message) << "\",\"resources\":[";
    for (size_t index = 0; index < result.resources.size(); ++index) {
        if (index) json << ',';
        json << "{\"id\":" << result.resources[index].id
             << ",\"ownerPid\":" << result.resources[index].ownerPid << '}';
    }
    json << "],\"requests\":[";
    for (size_t index = 0; index < result.requests.size(); ++index) {
        if (index) json << ',';
        json << "{\"pid\":" << result.requests[index].pid
             << ",\"resourceId\":" << result.requests[index].resourceId << '}';
    }
    json << "],\"releasedResourceIds\":[";
    for (size_t index = 0; index < result.releasedResourceIds.size(); ++index) {
        if (index) json << ',';
        json << result.releasedResourceIds[index];
    }
    json << "]}";
    return json.str();
}

std::string resourceActionFromForm(const std::unordered_map<std::string, std::string>& values) {
    ResourceGraphInput graph = resourceGraphFromForm(values);
    auto actionValue = values.find("action");
    if (actionValue == values.end()) throw std::invalid_argument("Choose a resource action.");
    int pid = formInteger(values, "pid", 0, std::numeric_limits<int>::max());
    int resourceId = formInteger(values, "resourceId", 0, std::numeric_limits<int>::max());
    ResourceActionResult result;
    std::string error;
    if (!applyResourceAction(graph.processIds, graph.resources, graph.requests,
            actionValue->second, pid, resourceId, result, error)) {
        throw std::invalid_argument(error);
    }
    return resourceActionJson(result);
}

bool sendAll(int socket, const char* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        ssize_t result = send(socket, data + sent, size - sent, 0);
        if (result <= 0) return false;
        sent += static_cast<size_t>(result);
    }
    return true;
}

void respond(int socket, int status, const std::string& reason,
             const std::string& contentType, const std::string& body) {
    std::ostringstream headers;
    headers << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
            << "Content-Type: " << contentType << "\r\n"
            << "Content-Length: " << body.size() << "\r\n"
            << "Cache-Control: no-store\r\n"
            << "Connection: close\r\n\r\n";
    std::string headerText = headers.str();
    sendAll(socket, headerText.data(), headerText.size());
    sendAll(socket, body.data(), body.size());
}

void respondJson(int socket, int status, const std::string& reason,
                 const std::string& body) {
    respond(socket, status, reason, "application/json; charset=utf-8", body);
}

void respondError(int socket, int status, const std::string& reason,
                  const std::string& message) {
    respondJson(socket, status, reason,
        "{\"error\":\"" + jsonEscape(message) + "\"}");
}

bool readRequest(int socket, HttpRequest& request) {
    std::string received;
    std::array<char, 8192> buffer{};
    size_t headerEnd = std::string::npos;

    while ((headerEnd = received.find("\r\n\r\n")) == std::string::npos) {
        ssize_t count = recv(socket, buffer.data(), buffer.size(), 0);
        if (count <= 0) return false;
        received.append(buffer.data(), static_cast<size_t>(count));
        if (received.size() > 32768) return false;
    }

    std::istringstream headerStream(received.substr(0, headerEnd));
    std::string requestLine;
    if (!std::getline(headerStream, requestLine)) return false;
    std::istringstream requestParts(requestLine);
    std::string version;
    if (!(requestParts >> request.method >> request.target >> version)) return false;

    std::string line;
    while (std::getline(headerStream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        request.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }

    size_t queryMark = request.target.find('?');
    request.path = request.target.substr(0, queryMark);
    request.query = queryMark == std::string::npos ? "" : request.target.substr(queryMark + 1);
    auto length = request.headers.find("content-length");
    if (length != request.headers.end()) {
        try {
            request.contentLength = std::stoull(length->second);
        } catch (...) {
            return false;
        }
    }

    request.body = received.substr(headerEnd + 4);
    if (request.body.size() > request.contentLength) {
        request.body.resize(static_cast<size_t>(request.contentLength));
    }
    return true;
}

bool receiveBody(int socket, HttpRequest& request, size_t maximumSize) {
    if (request.contentLength > maximumSize) return false;
    request.body.reserve(static_cast<size_t>(request.contentLength));
    std::array<char, 8192> buffer{};
    while (request.body.size() < request.contentLength) {
        size_t remaining = static_cast<size_t>(request.contentLength) - request.body.size();
        ssize_t count = recv(socket, buffer.data(), std::min(buffer.size(), remaining), 0);
        if (count <= 0) return false;
        request.body.append(buffer.data(), static_cast<size_t>(count));
    }
    return true;
}

bool streamBodyToFile(int socket, const HttpRequest& request, const fs::path& path) {
    if (request.contentLength == 0) return false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;

    size_t written = std::min<unsigned long long>(request.body.size(), request.contentLength);
    output.write(request.body.data(), static_cast<std::streamsize>(written));
    std::array<char, 65536> buffer{};
    while (written < request.contentLength) {
        size_t remaining = static_cast<size_t>(request.contentLength - written);
        ssize_t count = recv(socket, buffer.data(), std::min(buffer.size(), remaining), 0);
        if (count <= 0) {
            output.close();
            fs::remove(path);
            return false;
        }
        output.write(buffer.data(), count);
        if (!output) {
            output.close();
            fs::remove(path);
            return false;
        }
        written += static_cast<size_t>(count);
    }
    output.close();
    return static_cast<bool>(output);
}

std::string headerValue(const HttpRequest& request, const std::string& name) {
    auto found = request.headers.find(lower(name));
    return found == request.headers.end() ? "" : found->second;
}

std::string fileId() {
    static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static std::random_device random;
    std::string value = "FILE_";
    for (int i = 0; i < 4; ++i) value += alphabet[random() % alphabet.size()];
    return value;
}

std::string chunkId() {
    static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static std::random_device random;
    std::string value = "CHUNK_";
    for (int i = 0; i < 4; ++i) value += alphabet[random() % alphabet.size()];
    return value;
}

uintmax_t calculateChunkSize(uintmax_t fileSize) {
    const uintmax_t minimum = 64ULL * 1024ULL;
    const uintmax_t maximum = 10ULL * 1024ULL * 1024ULL * 1024ULL;
    const double baseChunk = 256.0 * 1024.0 * 1024.0;
    const double baseFile = 1024.0 * 1024.0 * 1024.0;
    double size = baseChunk * std::pow(static_cast<double>(fileSize) / baseFile, 0.65);
    uintmax_t result = static_cast<uintmax_t>(size);
    result = std::max(result, minimum);
    result = std::min(result, maximum);
    return std::min(result, fileSize);
}

std::string checksum(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return "";
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    std::array<char, 65536> buffer{};
    while (input.read(buffer.data(), buffer.size()) || input.gcount() > 0) {
        CC_SHA256_Update(&context, buffer.data(), static_cast<CC_LONG>(input.gcount()));
    }
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &context);
    std::string result;
    for (unsigned char byte : digest) {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02x", byte);
        result += hex;
    }
    return result;
}

std::string copyStatus(const std::string& path, const std::string& expected) {
    if (path.empty() || !fs::exists(path) || !fs::is_regular_file(path)) return "MISSING";
    std::string actual = checksum(path);
    if (actual.empty()) return "ERROR";
    return actual == expected ? "HEALTHY" : "CORRUPTED";
}

CopyStatus statuses(const ChunkRecord& chunk) {
    return {
        copyStatus(chunk.mainLocation, chunk.checksum),
        copyStatus(chunk.node1Location, chunk.checksum),
        copyStatus(chunk.node2Location, chunk.checksum)
    };
}

std::string statusJson(const ChunkRecord& chunk, const CopyStatus& status) {
    std::ostringstream json;
    json << "{\"id\":\"" << jsonEscape(chunk.chunkId)
         << "\",\"fileId\":\"" << jsonEscape(chunk.fileId)
         << "\",\"number\":" << chunk.chunkNumber
         << ",\"size\":" << chunk.chunkSize
         << ",\"checksum\":\"" << jsonEscape(chunk.checksum)
         << "\",\"main\":{\"path\":\"" << jsonEscape(chunk.mainLocation)
         << "\",\"status\":\"" << status.mainStatus
         << "\"},\"node1\":{\"path\":\"" << jsonEscape(chunk.node1Location)
         << "\",\"status\":\"" << status.node1Status
         << "\"},\"node2\":{\"path\":\"" << jsonEscape(chunk.node2Location)
         << "\",\"status\":\"" << status.node2Status << "\"}}";
    return json.str();
}

std::string chunksJson(const std::vector<ChunkRecord>& chunks) {
    std::ostringstream json;
    json << '[';
    for (size_t i = 0; i < chunks.size(); ++i) {
        if (i) json << ',';
        json << statusJson(chunks[i], statuses(chunks[i]));
    }
    json << ']';
    return json.str();
}

std::string activitiesJson(const std::vector<ActivityRecord>& activities) {
    std::ostringstream json;
    json << '[';
    for (size_t i = 0; i < activities.size(); ++i) {
        if (i) json << ',';
        json << "{\"id\":\"" << activities[i].id << "\""
             << ",\"action\":\"" << jsonEscape(activities[i].action)
             << "\",\"details\":\"" << jsonEscape(activities[i].details)
             << "\",\"time\":\"" << jsonEscape(activities[i].createdAt) << "\"}";
    }
    json << ']';
    return json.str();
}

std::string filesJson(const std::vector<FileRecord>& files) {
    std::ostringstream json;
    json << '[';
    for (size_t i = 0; i < files.size(); ++i) {
        if (i) json << ',';
        json << "{\"id\":\"" << jsonEscape(files[i].fileId)
             << "\",\"name\":\"" << jsonEscape(files[i].fileName)
             << "\",\"size\":" << files[i].originalSize
             << ",\"chunkCount\":" << files[i].chunkCount << '}';
    }
    json << ']';
    return json.str();
}

bool loadFileData(Database& database, std::vector<FileRecord>& files,
                  std::vector<ChunkRecord>& chunks) {
    return database.getFiles(files) && database.getChunks(chunks);
}

std::string dashboardJson(Database& database) {
    std::vector<FileRecord> files;
    std::vector<ChunkRecord> chunks;
    std::vector<ActivityRecord> activities;
    unsigned long long recoveries = 0;
    unsigned long long deletedFiles = 0;
    if (!loadFileData(database, files, chunks) || !database.getActivities(activities) ||
        !database.getSuccessfulRecoveryCount(recoveries) ||
        !database.getDeletedFileCount(deletedFiles)) {
        throw std::runtime_error("Unable to read ChunkVault records from MySQL.");
    }

    unsigned long long healthy = 0;
    unsigned long long corrupted = 0;
    unsigned long long missing = 0;
    for (const auto& chunk : chunks) {
        CopyStatus result = statuses(chunk);
        for (const auto* value : {&result.mainStatus, &result.node1Status, &result.node2Status}) {
            if (*value == "HEALTHY") ++healthy;
            else if (*value == "CORRUPTED") ++corrupted;
            else if (*value == "MISSING") ++missing;
        }
    }

    std::ostringstream json;
    json << "{\"fileCount\":" << files.size()
         << ",\"chunkCount\":" << chunks.size()
         << ",\"copyCount\":" << chunks.size() * 3
         << ",\"healthyCopies\":" << healthy
         << ",\"corruptedCopies\":" << corrupted
         << ",\"physicalMissingCopies\":" << missing
         << ",\"deletedFileCount\":" << deletedFiles
         << ",\"missingCopies\":" << missing + deletedFiles
         << ",\"successfulRecoveries\":" << recoveries
         << ",\"files\":" << filesJson(files)
         << ",\"activities\":" << activitiesJson(activities) << '}';
    return json.str();
}

std::string databaseJson(Database& database) {
    std::vector<FileRecord> files;
    std::vector<ChunkRecord> chunks;
    std::vector<ActivityRecord> activities;
    if (!loadFileData(database, files, chunks) || !database.getActivities(activities)) {
        throw std::runtime_error("Unable to read ChunkVault records from MySQL.");
    }
    return "{\"files\":" + filesJson(files) + ",\"chunks\":" +
        chunksJson(chunks) + ",\"activities\":" + activitiesJson(activities) + '}';
}

std::string storeUploadedFile(const fs::path& temporaryPath, const std::string& filename,
                              const std::string& mode, unsigned int requestedChunks) {
    uintmax_t size = fs::file_size(temporaryPath);
    if (size == 0) throw std::invalid_argument("The selected file is empty.");

    uintmax_t chunkSize = 0;
    uintmax_t totalChunks = 0;
    if (mode == "automatic") {
        chunkSize = calculateChunkSize(size);
        totalChunks = (size + chunkSize - 1) / chunkSize;
    } else if (mode == "manual") {
        if (requestedChunks == 0 || requestedChunks > size) {
            throw std::invalid_argument("The number of chunks must be between 1 and the file size in bytes.");
        }
        totalChunks = requestedChunks;
        chunkSize = (size + totalChunks - 1) / totalChunks;
    } else {
        throw std::invalid_argument("Choose automatic or manual chunking.");
    }

    Database database;
    if (!database.connect()) throw std::runtime_error("MySQL is unavailable; the file was not stored.");

    std::string id;
    fs::path fileDirectory;
    for (int attempt = 0; attempt < 20; ++attempt) {
        id = fileId();
        fileDirectory = fs::path("storage") / (filename + " (" + id + ")");
        if (!fs::exists(fileDirectory)) break;
        id.clear();
    }
    if (id.empty()) throw std::runtime_error("Unable to generate a unique file ID.");

    if (!database.insertFile(id, filename, size, static_cast<unsigned int>(totalChunks))) {
        throw std::runtime_error("MySQL did not save the file metadata; no storage job was started.");
    }

    fs::path chunksDirectory = fileDirectory / "chunks";
    fs::path node1Directory = fileDirectory / "node1";
    fs::path node2Directory = fileDirectory / "node2";
    fs::create_directories(chunksDirectory);
    fs::create_directories(node1Directory);
    fs::create_directories(node2Directory);

    std::ifstream input(temporaryPath, std::ios::binary);
    if (!input) throw std::runtime_error("Unable to reopen the uploaded file.");

    std::vector<std::string> resultChunks;
    std::vector<char> buffer(4ULL * 1024ULL * 1024ULL);
    uintmax_t bytesRemaining = size;
    unsigned int number = 1;
    while (bytesRemaining > 0) {
        std::string generatedChunkId = chunkId();
        fs::path mainPath = chunksDirectory / ("chunk_" + std::to_string(number) + ".chunk");
        fs::path node1Path = node1Directory / mainPath.filename();
        fs::path node2Path = node2Directory / mainPath.filename();
        std::ofstream mainOutput(mainPath, std::ios::binary);
        std::ofstream node1Output(node1Path, std::ios::binary);
        std::ofstream node2Output(node2Path, std::ios::binary);
        if (!mainOutput || !node1Output || !node2Output) {
            throw std::runtime_error("Unable to create a chunk or replica file.");
        }

        uintmax_t chunkBytes = 0;
        while (chunkBytes < chunkSize && bytesRemaining > 0) {
            uintmax_t amount = std::min<uintmax_t>(
                {static_cast<uintmax_t>(buffer.size()), chunkSize - chunkBytes, bytesRemaining});
            input.read(buffer.data(), static_cast<std::streamsize>(amount));
            std::streamsize readBytes = input.gcount();
            if (readBytes <= 0) throw std::runtime_error("The uploaded file could not be read completely.");
            mainOutput.write(buffer.data(), readBytes);
            node1Output.write(buffer.data(), readBytes);
            node2Output.write(buffer.data(), readBytes);
            if (!mainOutput || !node1Output || !node2Output) {
                throw std::runtime_error("Unable to write a chunk replica.");
            }
            chunkBytes += static_cast<uintmax_t>(readBytes);
            bytesRemaining -= static_cast<uintmax_t>(readBytes);
        }
        mainOutput.close();
        node1Output.close();
        node2Output.close();

        std::string hash = checksum(mainPath);
        if (hash.empty()) throw std::runtime_error("Unable to calculate the chunk checksum.");
        if (!database.insertChunk(
                generatedChunkId, id, number, chunkBytes, hash,
                mainPath.string(), node1Path.string(), node2Path.string())) {
            throw std::runtime_error("MySQL did not save chunk metadata.");
        }

        std::ofstream metadata("storage/chunk_metadata.txt", std::ios::app);
        if (!metadata) throw std::runtime_error("Unable to append the local chunk metadata file.");
        metadata << id << '|' << filename << '|' << mainPath.string() << '|'
                 << node1Path.string() << '|' << node2Path.string() << '|' << hash << '\n';
        if (!metadata) throw std::runtime_error("Unable to save the local chunk metadata record.");

        resultChunks.push_back(
            "{\"id\":\"" + jsonEscape(generatedChunkId) + "\",\"number\":" +
            std::to_string(number) + ",\"size\":" + std::to_string(chunkBytes) +
            ",\"checksum\":\"" + hash + "\"}");
        ++number;
    }

    fs::remove(temporaryPath);
    bool activityLogged = database.logActivity("STORAGE", filename + " (" + id + ") stored with " +
        std::to_string(totalChunks) + " chunk(s).");

    std::ostringstream json;
    json << "{\"id\":\"" << id << "\",\"name\":\"" << jsonEscape(filename)
         << "\",\"size\":" << size << ",\"chunkCount\":" << totalChunks
         << ",\"activityLogged\":" << (activityLogged ? "true" : "false")
         << ",\"chunks\":[";
    for (size_t i = 0; i < resultChunks.size(); ++i) {
        if (i) json << ',';
        json << resultChunks[i];
    }
    json << "]}";
    return json.str();
}

bool findChunks(Database& database, const std::string& id, std::vector<ChunkRecord>& chunks) {
    return database.getChunks(chunks, id);
}

std::string verifyFile(Database& database, const std::string& id) {
    std::vector<ChunkRecord> chunks;
    if (!findChunks(database, id, chunks)) throw std::runtime_error("Unable to load chunk records from MySQL.");
    if (chunks.empty()) throw std::invalid_argument("No chunks were found for this file ID.");
    if (!database.logActivity("VERIFY", "Verified " + id + " (" + std::to_string(chunks.size()) + " chunk(s)).")) {
        throw std::runtime_error("Chunk records were read, but the verification log could not be saved.");
    }
    return "{\"fileId\":\"" + jsonEscape(id) + "\",\"chunks\":" + chunksJson(chunks) + '}';
}

std::string recoverFile(Database& database, const std::string& id) {
    std::vector<ChunkRecord> chunks;
    if (!findChunks(database, id, chunks)) throw std::runtime_error("Unable to load chunk records from MySQL.");
    if (chunks.empty()) throw std::invalid_argument("No chunks were found for this file ID.");

    unsigned int restored = 0;
    for (const auto& chunk : chunks) {
        CopyStatus current = statuses(chunk);
        std::string source;
        if (current.mainStatus == "HEALTHY") source = chunk.mainLocation;
        else if (current.node1Status == "HEALTHY") source = chunk.node1Location;
        else if (current.node2Status == "HEALTHY") source = chunk.node2Location;
        if (source.empty()) continue;

        for (const auto& target : {chunk.mainLocation, chunk.node1Location, chunk.node2Location}) {
            if (target == source || copyStatus(target, chunk.checksum) == "HEALTHY") continue;
            std::error_code error;
            fs::create_directories(fs::path(target).parent_path(), error);
            if (error) continue;
            fs::copy_file(source, target, fs::copy_options::overwrite_existing, error);
            if (!error && copyStatus(target, chunk.checksum) == "HEALTHY") ++restored;
        }
    }

    bool logged = true;
    if (restored > 0) {
        logged = database.logActivity("RECOVERY", id + ": restored " + std::to_string(restored) + " copy/copies.");
    } else {
        logged = database.logActivity("RECOVERY_NOOP", id + ": no unhealthy copy could be restored.");
    }
    if (!logged) throw std::runtime_error("Recovery ran, but the operation log could not be saved.");

    return "{\"fileId\":\"" + jsonEscape(id) + "\",\"restoredCopies\":" +
        std::to_string(restored) + ",\"chunks\":" + chunksJson(chunks) + '}';
}

std::string deleteStoredFile(Database& database, const std::string& id) {
    std::vector<FileRecord> files;
    if (!database.getFiles(files)) throw std::runtime_error("Unable to read file metadata from MySQL.");
    auto found = std::find_if(files.begin(), files.end(), [&id](const FileRecord& file) {
        return file.fileId == id;
    });
    if (found == files.end()) throw std::invalid_argument("The selected file does not exist.");

    const std::string fileIdValue = found->fileId;
    const std::string fileName = fs::path(found->fileName).filename().string();
    if (fileName.empty() || fileName == "." || fileName == "..") {
        throw std::runtime_error("The file name in MySQL is invalid; no data was deleted.");
    }
    const fs::path directory = fs::path("storage") / (fileName + " (" + fileIdValue + ")");
    std::error_code error;
    fs::remove_all(directory, error);
    if (error) throw std::runtime_error("Unable to remove the file chunks and replicas: " + error.message());

    const fs::path metadata = fs::path("storage") / "chunk_metadata.txt";
    if (fs::exists(metadata)) {
        const fs::path temporary = fs::path("storage") / "chunk_metadata.txt.tmp";
        std::ifstream input(metadata);
        std::ofstream output(temporary, std::ios::trunc);
        if (!input || !output) throw std::runtime_error("Unable to update the local chunk log.");
        std::string line;
        const std::string prefix = fileIdValue + "|";
        while (std::getline(input, line)) {
            if (line.rfind(prefix, 0) != 0) output << line << '\n';
        }
        input.close();
        output.close();
        if (!output) throw std::runtime_error("Unable to write the updated local chunk log.");
        fs::rename(temporary, metadata, error);
        if (error) {
            fs::remove(temporary);
            throw std::runtime_error("Unable to replace the local chunk log: " + error.message());
        }
    }

    if (!database.deleteFile(fileIdValue)) {
        throw std::runtime_error("Chunks were removed, but MySQL could not remove the file records and logs.");
    }
    return "{\"deleted\":true,\"fileId\":\"" + jsonEscape(fileIdValue) + "\"}";
}

std::string fileContentJson(Database& database, const std::string& id) {
    std::vector<ChunkRecord> chunks;
    if (!findChunks(database, id, chunks)) throw std::runtime_error("Unable to load chunk records from MySQL.");
    if (chunks.empty()) throw std::invalid_argument("No chunks were found for this file ID.");
    const size_t maximumBytes = 1024 * 1024;
    std::string content;
    content.reserve(maximumBytes);
    bool allVerified = true;
    bool truncated = false;
    for (size_t index = 0; index < chunks.size(); ++index) {
        const ChunkRecord& chunk = chunks[index];
        CopyStatus status = statuses(chunk);
        std::array<std::pair<std::string, std::string>, 3> copies = {{
            {chunk.mainLocation, status.mainStatus},
            {chunk.node1Location, status.node1Status},
            {chunk.node2Location, status.node2Status}
        }};
        auto source = std::find_if(copies.begin(), copies.end(), [](const auto& copy) {
            return copy.second == "HEALTHY";
        });
        if (source == copies.end()) {
            source = std::find_if(copies.begin(), copies.end(), [](const auto& copy) {
                return !copy.first.empty() && fs::exists(copy.first) && fs::is_regular_file(copy.first);
            });
            allVerified = false;
        }
        if (source == copies.end()) throw std::runtime_error("No readable copy exists for chunk " + std::to_string(chunk.chunkNumber) + ".");

        std::ifstream input(source->first, std::ios::binary);
        if (!input) throw std::runtime_error("Unable to read chunk " + std::to_string(chunk.chunkNumber) + ".");
        std::array<char, 65536> buffer{};
        while (content.size() < maximumBytes) {
            size_t amount = std::min(buffer.size(), maximumBytes - content.size());
            input.read(buffer.data(), static_cast<std::streamsize>(amount));
            std::streamsize received = input.gcount();
            if (received > 0) content.append(buffer.data(), static_cast<size_t>(received));
            if (input.bad()) throw std::runtime_error("Unable to read chunk " + std::to_string(chunk.chunkNumber) + ".");
            if (input.eof() || received == 0) break;
        }
        if (content.size() == maximumBytes) {
            truncated = input.peek() != EOF || index + 1 < chunks.size();
            break;
        }
    }
    static const char digits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(content.size() * 2);
    for (unsigned char byte : content) {
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 15]);
    }
    return "{\"fileId\":\"" + jsonEscape(id) + "\",\"verified\":" +
        (allVerified ? "true" : "false") + ",\"truncated\":" +
        (truncated ? "true" : "false") + ",\"contentHex\":\"" + hex + "\"}";
}

std::string mimeType(const std::string& path) {
    auto endsWith = [&path](const std::string& suffix) {
        return path.size() >= suffix.size() &&
            path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    if (endsWith(".html")) return "text/html; charset=utf-8";
    if (endsWith(".css")) return "text/css; charset=utf-8";
    if (endsWith(".js")) return "text/javascript; charset=utf-8";
    return "application/octet-stream";
}

void serveStatic(int socket, const std::string& path) {
    std::string fileName;
    if (path == "/" || path == "/index.html") fileName = "index.html";
    else if (path == "/style.css") fileName = "style.css";
    else if (path == "/script.js") fileName = "script.js";
    else {
        respondError(socket, 404, "Not Found", "Page not found.");
        return;
    }

    std::ifstream input(fs::path("web") / fileName, std::ios::binary);
    if (!input) {
        respondError(socket, 404, "Not Found", "The web page files were not found. Start ChunkVault from its project folder.");
        return;
    }
    std::ostringstream content;
    content << input.rdbuf();
    std::string body = content.str();
    respond(socket, 200, "OK", mimeType(fileName), body);
}

void handleApi(int socket, HttpRequest& request) {
    const auto query = parseQuery(request.query);
    try {
        if (request.method == "GET" && request.path == "/api/dashboard") {
            Database database;
            if (!database.connect()) throw std::runtime_error("MySQL is unavailable.");
            respondJson(socket, 200, "OK", dashboardJson(database));
            return;
        }

        if (request.method == "GET" && request.path == "/api/files") {
            Database database;
            std::vector<FileRecord> files;
            if (!database.connect() || !database.getFiles(files)) {
                throw std::runtime_error("Unable to read file metadata from MySQL.");
            }
            respondJson(socket, 200, "OK", filesJson(files));
            return;
        }

        if (request.method == "GET" && request.path == "/api/database") {
            Database database;
            if (!database.connect()) throw std::runtime_error("MySQL is unavailable.");
            respondJson(socket, 200, "OK", databaseJson(database));
            return;
        }

        if (request.method == "POST" && request.path == "/api/os/schedule") {
            if (!receiveBody(socket, request, 524288)) {
                respondError(socket, 413, "Payload Too Large", "The scheduling request is too large.");
                return;
            }
            auto values = parseQuery(request.body);
            auto processes = schedulingProcesses(values);
            auto algorithmValue = values.find("algorithm");
            if (algorithmValue == values.end()) throw std::invalid_argument("Choose a scheduling algorithm.");
            int quantum = formInteger(values, "quantum", 0, std::numeric_limits<int>::max());
            ScheduleResult result;
            std::string error;
            if (!calculateSchedule(processes, algorithmValue->second, quantum, result, error)) {
                throw std::invalid_argument(error);
            }
            respondJson(socket, 200, "OK", scheduleJson(result));
            return;
        }

        if (request.method == "POST" && request.path == "/api/os/deadlock") {
            if (!receiveBody(socket, request, 524288)) {
                respondError(socket, 413, "Payload Too Large", "The deadlock detection request is too large.");
                return;
            }
            auto cycle = deadlockCycleFromForm(parseQuery(request.body));
            std::ostringstream json;
            json << "{\"deadlocked\":" << (!cycle.empty() ? "true" : "false") << ",\"cycle\":[";
            for (size_t index = 0; index < cycle.size(); ++index) {
                if (index) json << ',';
                json << cycle[index];
            }
            json << "]}";
            respondJson(socket, 200, "OK", json.str());
            return;
        }

        if (request.method == "POST" && request.path == "/api/os/resources") {
            if (!receiveBody(socket, request, 524288)) {
                respondError(socket, 413, "Payload Too Large", "The resource action request is too large.");
                return;
            }
            respondJson(socket, 200, "OK", resourceActionFromForm(parseQuery(request.body)));
            return;
        }

        if (request.method == "POST" && request.path == "/api/activity") {
            if (!receiveBody(socket, request, 65536)) {
                respondError(socket, 413, "Payload Too Large", "The activity record is too large.");
                return;
            }
            auto bodyValues = parseQuery(request.body);
            std::string action = bodyValues["action"];
            std::string details = bodyValues["details"];
            if (action.empty() || details.empty()) {
                respondError(socket, 400, "Bad Request", "An action and description are required.");
                return;
            }
            Database database;
            if (!database.connect() || !database.logActivity(action, details)) {
                throw std::runtime_error("Unable to save the activity to MySQL.");
            }
            respondJson(socket, 201, "Created", "{\"saved\":true}");
            return;
        }

        const std::string activityPrefix = "/api/activity/";
        if (request.method == "DELETE" && request.path.rfind(activityPrefix, 0) == 0) {
            const std::string rawId = request.path.substr(activityPrefix.size());
            if (rawId.empty() || rawId.find_first_not_of("0123456789") != std::string::npos) {
                respondError(socket, 400, "Bad Request", "A valid activity ID is required.");
                return;
            }
            unsigned long long activityId = 0;
            try {
                activityId = std::stoull(rawId);
            } catch (...) {
                respondError(socket, 400, "Bad Request", "The activity ID is out of range.");
                return;
            }
            Database database;
            if (!database.connect() || !database.deleteActivity(activityId)) {
                throw std::runtime_error("Unable to delete the activity log from MySQL.");
            }
            respondJson(socket, 200, "OK", "{\"deleted\":true}");
            return;
        }

        if (request.method == "POST" && request.path == "/api/files") {
            std::string encodedName = headerValue(request, "x-file-name");
            std::string filename = urlDecode(encodedName);
            if (filename.find('\0') != std::string::npos) {
                respondError(socket, 400, "Bad Request", "The file name is invalid.");
                return;
            }
            filename = fs::path(filename).filename().string();
            if (filename.empty() || filename == "." || filename == "..") {
                respondError(socket, 400, "Bad Request", "A valid file name is required.");
                return;
            }
            std::string mode = headerValue(request, "x-chunk-mode");
            unsigned int requestedChunks = 0;
            if (mode == "manual") {
                try {
                    requestedChunks = static_cast<unsigned int>(std::stoul(headerValue(request, "x-chunk-count")));
                } catch (...) {
                    respondError(socket, 400, "Bad Request", "Enter a valid number of chunks.");
                    return;
                }
            }

            fs::path tempDirectory = fs::path("storage") / ".uploads";
            fs::create_directories(tempDirectory);
            fs::path temporaryPath = tempDirectory / ("upload_" + fileId());
            if (!streamBodyToFile(socket, request, temporaryPath)) {
                respondError(socket, 400, "Bad Request", "The uploaded file could not be received.");
                return;
            }
            try {
                std::string result = storeUploadedFile(temporaryPath, filename, mode, requestedChunks);
                respondJson(socket, 201, "Created", result);
            } catch (...) {
                std::error_code error;
                fs::remove(temporaryPath, error);
                throw;
            }
            return;
        }

        const std::string filePrefix = "/api/files/";
        if (request.path.rfind(filePrefix, 0) == 0) {
            std::string suffix = request.path.substr(filePrefix.size());
            size_t separator = suffix.find('/');
            std::string id = separator == std::string::npos ? suffix : suffix.substr(0, separator);
            std::string action = separator == std::string::npos ? "" : suffix.substr(separator + 1);
            if (id.empty()) {
                respondError(socket, 400, "Bad Request", "A file ID is required.");
                return;
            }
            Database database;
            if (!database.connect()) throw std::runtime_error("MySQL is unavailable.");

            if (request.method == "DELETE" && action.empty()) {
                respondJson(socket, 200, "OK", deleteStoredFile(database, id));
                return;
            }

            if (request.method == "GET" && action == "chunks") {
                std::vector<ChunkRecord> chunks;
                if (!findChunks(database, id, chunks)) throw std::runtime_error("Unable to read chunk metadata from MySQL.");
                respondJson(socket, 200, "OK", "{\"fileId\":\"" + jsonEscape(id) + "\",\"chunks\":" + chunksJson(chunks) + '}');
                return;
            }
            if (request.method == "POST" && action == "verify") {
                respondJson(socket, 200, "OK", verifyFile(database, id));
                return;
            }
            if (request.method == "POST" && action == "recover") {
                respondJson(socket, 200, "OK", recoverFile(database, id));
                return;
            }
            if (request.method == "GET" && action == "content") {
                respondJson(socket, 200, "OK", fileContentJson(database, id));
                return;
            }
        }

        respondError(socket, 404, "Not Found", "API endpoint not found.");
    } catch (const std::invalid_argument& error) {
        respondError(socket, 400, "Bad Request", error.what());
    } catch (const std::exception& error) {
        respondError(socket, 500, "Internal Server Error", error.what());
    }
}

void handleConnection(int socket) {
    HttpRequest request;
    if (!readRequest(socket, request)) {
        respondError(socket, 400, "Bad Request", "The HTTP request could not be read.");
        return;
    }
    std::string origin = headerValue(request, "origin");
    if (!origin.empty() && origin != allowedOrigin) {
        respondError(socket, 403, "Forbidden", "Requests from this origin are not allowed.");
        return;
    }
    if (request.path.rfind("/api/", 0) == 0) {
        handleApi(socket, request);
    } else if (request.method == "GET") {
        serveStatic(socket, request.path);
    } else {
        respondError(socket, 405, "Method Not Allowed", "This request method is not supported.");
    }
}

}

int runWebServer(unsigned short port) {
    allowedOrigin = "http://127.0.0.1:" + std::to_string(port);
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        std::cerr << "Unable to create the web server socket.\n";
        return 1;
    }

    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);

    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        listen(server, 16) < 0) {
        std::cerr << "Unable to start ChunkVault web server on port " << port << ".\n";
        close(server);
        return 1;
    }

    std::cout << "ChunkVault is available at http://127.0.0.1:" << port << "/\n";
    std::cout << "Press Ctrl+C to stop the server.\n";
    for (;;) {
        int client = accept(server, nullptr, nullptr);
        if (client < 0) {
            if (errno == EINTR) continue;
            std::cerr << "Unable to accept a web request.\n";
            continue;
        }
        handleConnection(client);
        shutdown(client, SHUT_RDWR);
        close(client);
    }

    close(server);
    return 0;
}
