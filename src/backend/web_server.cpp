#include "../../database/Database.h"
#include "http.h"
#include "os_simulator.h"
#include "storage.h"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace fs = std::filesystem;
using std::to_string;

namespace {

constexpr int kMaxInt = std::numeric_limits<int>::max();
constexpr size_t kFormLimit = 524288;

std::string allowedOrigin;

struct PayloadTooLarge : std::runtime_error { using std::runtime_error::runtime_error; };

const auto intJson = [](int value) { return to_string(value); };

int formInt(const Form& form, const std::string& key, int minimum, int maximum) {
    const auto found = form.find(key);
    if (found == form.end() || found->second.empty()) {
        throw std::invalid_argument("The OS simulation request is missing " + key + ".");
    }
    long long value = 0;
    size_t used = 0;
    try { value = std::stoll(found->second, &used); }
    catch (...) { throw std::invalid_argument("The OS simulation value for " + key + " is invalid."); }
    if (used != found->second.size() || value < minimum || value > maximum) {
        throw std::invalid_argument("The OS simulation value for " + key + " is outside its allowed range.");
    }
    return static_cast<int>(value);
}

std::vector<Process> processesFromForm(const Form& form) {
    const int count = formInt(form, "count", 1, 256);
    std::vector<Process> processes;
    for (int i = 0; i < count; ++i) {
        const std::string n = to_string(i);
        Process p;
        p.pid = formInt(form, "pid" + n, 1, kMaxInt);
        p.arrivalTime = formInt(form, "arrival" + n, 0, kMaxInt);
        p.burstTime = formInt(form, "burst" + n, 1, kMaxInt);
        const auto name = form.find("name" + n);
        p.name = name == form.end() || name->second.empty() ? "P" + to_string(p.pid) : name->second;
        if (p.name.size() > 255) throw std::invalid_argument("Process names must be 255 characters or fewer.");
        processes.push_back(std::move(p));
    }
    return processes;
}

struct ResourceGraph {
    std::vector<int> processIds;
    std::vector<OSResource> resources;
    std::vector<OSResourceRequest> requests;
};

ResourceGraph graphFromForm(const Form& form) {
    ResourceGraph graph;
    std::unordered_set<int> processes, resources;

    const int processCount = formInt(form, "processCount", 0, 256);
    for (int i = 0; i < processCount; ++i) {
        const int pid = formInt(form, "processPid" + to_string(i), 1, kMaxInt);
        if (!processes.insert(pid).second) throw std::invalid_argument("Process IDs must be unique.");
        graph.processIds.push_back(pid);
    }
    const int resourceCount = formInt(form, "resourceCount", 0, 256);
    for (int i = 0; i < resourceCount; ++i) {
        const int id = formInt(form, "resourceId" + to_string(i), 1, kMaxInt);
        const int owner = formInt(form, "resourceOwner" + to_string(i), 0, kMaxInt);
        if (!resources.insert(id).second) throw std::invalid_argument("Resource IDs must be unique.");
        if (owner != 0 && !processes.count(owner)) throw std::invalid_argument("A resource owner is not in the process list.");
        graph.resources.push_back({id, owner});
    }
    const int requestCount = formInt(form, "requestCount", 0, 1024);
    for (int i = 0; i < requestCount; ++i) {
        const int pid = formInt(form, "requestPid" + to_string(i), 1, kMaxInt);
        const int id = formInt(form, "requestResource" + to_string(i), 1, kMaxInt);
        if (!processes.count(pid) || !resources.count(id)) {
            throw std::invalid_argument("A resource request refers to an unknown process or resource.");
        }
        graph.requests.push_back({pid, id});
    }
    return graph;
}

std::string scheduleJson(const ScheduleResult& r) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4)
        << "{\"algorithm\":" << quote(r.algorithm) << ",\"totalTime\":" << r.totalTime
        << ",\"averageWaiting\":" << r.averageWaiting << ",\"averageTurnaround\":" << r.averageTurnaround
        << ",\"cpuUtilization\":" << r.cpuUtilization << ",\"throughput\":" << r.throughput
        << ",\"gantt\":" << jsonArray(r.gantt, [](const GanttSegment& g) {
               return "{\"pid\":" + to_string(g.pid) + ",\"idle\":" + (g.idle ? "true" : "false") +
                      ",\"start\":" + to_string(g.start) + ",\"end\":" + to_string(g.end) + "}";
           })
        << ",\"results\":" << jsonArray(r.processes, [](const Process& p) {
               return "{\"pid\":" + to_string(p.pid) + ",\"name\":" + quote(p.name) +
                      ",\"arrivalTime\":" + to_string(p.arrivalTime) + ",\"burstTime\":" + to_string(p.burstTime) +
                      ",\"completionTime\":" + to_string(p.completionTime) + ",\"waitingTime\":" + to_string(p.waitingTime) +
                      ",\"turnaroundTime\":" + to_string(p.turnaroundTime) + "}";
           })
        << "}";
    return out.str();
}

std::string resourceActionJson(const ResourceActionResult& r) {
    return "{\"changedResourceId\":" + to_string(r.changedResourceId) + ",\"message\":" + quote(r.message) +
           ",\"resources\":" + jsonArray(r.resources, [](const OSResource& x) {
               return "{\"id\":" + to_string(x.id) + ",\"ownerPid\":" + to_string(x.ownerPid) + "}";
           }) +
           ",\"requests\":" + jsonArray(r.requests, [](const OSResourceRequest& x) {
               return "{\"pid\":" + to_string(x.pid) + ",\"resourceId\":" + to_string(x.resourceId) + "}";
           }) +
           ",\"releasedResourceIds\":" + jsonArray(r.releasedResourceIds, intJson) + "}";
}

std::string chunkJson(const ChunkRecord& c) {
    const auto paths = storage::locations(c);
    const auto states = storage::statuses(c);
    static const char* names[3] = {"main", "node1", "node2"};
    std::string json = "{\"id\":" + quote(c.chunkId) + ",\"fileId\":" + quote(c.fileId) +
                       ",\"number\":" + to_string(c.chunkNumber) + ",\"size\":" + to_string(c.chunkSize) +
                       ",\"checksum\":" + quote(c.checksum);
    for (int i = 0; i < 3; ++i) {
        json += std::string(",\"") + names[i] + "\":{\"path\":" + quote(paths[i]) + ",\"status\":" + quote(states[i]) + "}";
    }
    return json + "}";
}

std::string chunksJson(const std::vector<ChunkRecord>& chunks) { return jsonArray(chunks, chunkJson); }

std::string filesJson(const std::vector<FileRecord>& files) {
    return jsonArray(files, [](const FileRecord& f) {
        return "{\"id\":" + quote(f.fileId) + ",\"name\":" + quote(f.fileName) +
               ",\"size\":" + to_string(f.originalSize) + ",\"chunkCount\":" + to_string(f.chunkCount) +
               ",\"type\":" + quote(f.mimeType) + ",\"sha256\":" + quote(f.fileSha256) + "}";
    });
}

std::string activitiesJson(const std::vector<ActivityRecord>& activities) {
    return jsonArray(activities, [](const ActivityRecord& a) {
        return "{\"id\":" + quote(to_string(a.id)) + ",\"action\":" + quote(a.action) +
               ",\"details\":" + quote(a.details) + ",\"time\":" + quote(a.createdAt) + "}";
    });
}

void openDatabase(Database& database) {
    if (!database.connect()) throw std::runtime_error("MySQL is unavailable.");
}

std::vector<ChunkRecord> loadChunks(Database& database, const std::string& fileId, bool requireAny = true) {
    std::vector<ChunkRecord> chunks;
    if (!database.getChunks(chunks, fileId)) throw std::runtime_error("Unable to load chunk records from MySQL.");
    if (requireAny && chunks.empty()) throw std::invalid_argument("No chunks were found for this file ID.");
    return chunks;
}

std::string dashboardJson(Database& database) {
    std::vector<FileRecord> files;
    std::vector<ChunkRecord> chunks;
    std::vector<ActivityRecord> activities;
    unsigned long long recoveries = 0, deleted = 0;
    if (!database.getFiles(files) || !database.getChunks(chunks) || !database.getActivities(activities) ||
        !database.getSuccessfulRecoveryCount(recoveries) || !database.getDeletedFileCount(deleted)) {
        throw std::runtime_error("Unable to read ChunkVault records from MySQL.");
    }

    unsigned long long healthy = 0, corrupted = 0, missing = 0;
    for (const auto& chunk : chunks) {
        for (const auto& state : storage::statuses(chunk)) {
            if (state == "HEALTHY") ++healthy;
            else if (state == "CORRUPTED") ++corrupted;
            else if (state == "MISSING") ++missing;
        }
    }
    return "{\"fileCount\":" + to_string(files.size()) + ",\"chunkCount\":" + to_string(chunks.size()) +
           ",\"copyCount\":" + to_string(chunks.size() * 3) + ",\"healthyCopies\":" + to_string(healthy) +
           ",\"corruptedCopies\":" + to_string(corrupted) + ",\"physicalMissingCopies\":" + to_string(missing) +
           ",\"deletedFileCount\":" + to_string(deleted) + ",\"missingCopies\":" + to_string(missing + deleted) +
           ",\"successfulRecoveries\":" + to_string(recoveries) + ",\"files\":" + filesJson(files) +
           ",\"activities\":" + activitiesJson(activities) + "}";
}

std::string databaseJson(Database& database) {
    std::vector<FileRecord> files;
    std::vector<ChunkRecord> chunks;
    std::vector<ActivityRecord> activities;
    if (!database.getFiles(files) || !database.getChunks(chunks) || !database.getActivities(activities)) {
        throw std::runtime_error("Unable to read ChunkVault records from MySQL.");
    }
    return "{\"files\":" + filesJson(files) + ",\"chunks\":" + chunksJson(chunks) +
           ",\"activities\":" + activitiesJson(activities) + "}";
}

std::string verifyFile(Database& database, const std::string& id) {
    const auto chunks = loadChunks(database, id);
    if (!database.logActivity("VERIFY", "Verified " + id + " (" + to_string(chunks.size()) + " chunk(s)).")) {
        throw std::runtime_error("Chunk records were read, but the verification log could not be saved.");
    }
    return "{\"fileId\":" + quote(id) + ",\"chunks\":" + chunksJson(chunks) + "}";
}

std::string recoverFile(Database& database, const std::string& id) {
    const auto chunks = loadChunks(database, id);
    unsigned restored = 0;
    for (const auto& chunk : chunks) restored += storage::recover(chunk);

    const bool logged = restored > 0
        ? database.logActivity("RECOVERY", id + ": restored " + to_string(restored) + " copy/copies.")
        : database.logActivity("RECOVERY_NOOP", id + ": no unhealthy copy could be restored.");
    if (!logged) throw std::runtime_error("Recovery ran, but the operation log could not be saved.");

    return "{\"fileId\":" + quote(id) + ",\"restoredCopies\":" + to_string(restored) +
           ",\"chunks\":" + chunksJson(chunks) + "}";
}

std::string deleteStoredFile(Database& database, const std::string& id) {
    std::vector<FileRecord> files;
    if (!database.getFiles(files)) throw std::runtime_error("Unable to read file metadata from MySQL.");
    const auto found = std::find_if(files.begin(), files.end(), [&](const FileRecord& f) { return f.fileId == id; });
    if (found == files.end()) throw std::invalid_argument("The selected file does not exist.");

    const std::string name = fs::path(found->fileName).filename().string();
    if (name.empty() || name == "." || name == "..") {
        throw std::runtime_error("The file name in MySQL is invalid; no data was deleted.");
    }
    std::error_code error;
    fs::remove_all(fs::path("storage") / (name + " (" + id + ")"), error);
    if (error) throw std::runtime_error("Unable to remove the file chunks and replicas: " + error.message());

    if (!database.deleteFile(id)) {
        throw std::runtime_error("Chunks were removed, but MySQL could not remove the file records and logs.");
    }
    return "{\"deleted\":true,\"fileId\":" + quote(id) + "}";
}

std::string fileContentJson(Database& database, const std::string& id) {
    constexpr size_t kMaxBytes = 1024 * 1024;
    const auto chunks = loadChunks(database, id);

    std::string content;
    bool verified = true, truncated = false;
    std::vector<char> buffer(65536);
    for (size_t i = 0; i < chunks.size() && !truncated; ++i) {
        const auto paths = storage::locations(chunks[i]);
        const auto states = storage::statuses(chunks[i]);
        auto pick = std::find(states.begin(), states.end(), "HEALTHY");
        if (pick == states.end()) {
            verified = false;
            pick = std::find_if(states.begin(), states.end(), [](const std::string& s) { return s != "MISSING"; });
        }
        const std::string label = to_string(chunks[i].chunkNumber);
        if (pick == states.end()) throw std::runtime_error("No readable copy exists for chunk " + label + ".");

        std::ifstream input(paths[static_cast<size_t>(pick - states.begin())], std::ios::binary);
        if (!input) throw std::runtime_error("Unable to read chunk " + label + ".");
        while (input && content.size() < kMaxBytes) {
            input.read(buffer.data(), static_cast<std::streamsize>(std::min(buffer.size(), kMaxBytes - content.size())));
            content.append(buffer.data(), static_cast<size_t>(input.gcount()));
        }
        if (input.bad()) throw std::runtime_error("Unable to read chunk " + label + ".");
        truncated = content.size() == kMaxBytes && (input.peek() != EOF || i + 1 < chunks.size());
    }

    static const char digits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(content.size() * 2);
    for (unsigned char byte : content) {
        hex += digits[byte >> 4];
        hex += digits[byte & 15];
    }
    return "{\"fileId\":" + quote(id) + ",\"verified\":" + (verified ? "true" : "false") +
           ",\"truncated\":" + (truncated ? "true" : "false") + ",\"contentHex\":\"" + hex + "\"}";
}

std::string storeJson(const storage::StoreResult& result, const std::string& name) {
    return "{\"id\":" + quote(result.id) + ",\"name\":" + quote(name) + ",\"size\":" + to_string(result.size) +
           ",\"type\":" + quote(result.mimeType) + ",\"sha256\":" + quote(result.sha256) +
           ",\"chunkCount\":" + to_string(result.chunks.size()) +
           ",\"activityLogged\":" + (result.logged ? "true" : "false") +
           ",\"chunks\":" + jsonArray(result.chunks, [](const ChunkRecord& c) {
               return "{\"id\":" + quote(c.chunkId) + ",\"number\":" + to_string(c.chunkNumber) +
                      ",\"size\":" + to_string(c.chunkSize) + ",\"checksum\":" + quote(c.checksum) + "}";
           }) + "}";
}

void handleUpload(int socket, HttpRequest& request) {
    std::string filename = urlDecode(request.header("x-file-name"));
    if (filename.find('\0') != std::string::npos) throw std::invalid_argument("The file name is invalid.");
    filename = fs::path(filename).filename().string();
    if (filename.empty() || filename == "." || filename == "..") throw std::invalid_argument("A valid file name is required.");

    const std::string mode = request.header("x-chunk-mode");
    std::uintmax_t chunkCount = 0;
    if (mode == "manual") {
        try { chunkCount = std::stoull(request.header("x-chunk-count")); } catch (...) { chunkCount = 0; }
        if (chunkCount == 0) throw std::invalid_argument("Enter a valid number of chunks.");
    } else if (mode != "automatic") {
        throw std::invalid_argument("Choose automatic or manual chunking.");
    }

    fs::create_directories(fs::path("storage") / ".uploads");
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ignored; fs::remove(path, ignored); }
    } upload{fs::path("storage") / ".uploads" / storage::randomId("upload_", 8)};

    if (!streamBodyToFile(socket, request, upload.path)) throw std::invalid_argument("The uploaded file could not be received.");

    Database database;
    openDatabase(database);
    respondJson(socket, 201, storeJson(storage::storeFile(database, upload.path, filename, chunkCount), filename));
}

std::string asciiName(const std::string& name) {
    std::string out;
    for (unsigned char c : name) out += (c < 0x20 || c > 0x7E || c == '"' || c == '\\') ? '_' : static_cast<char>(c);
    return out;
}

void handleDownload(int socket, Database& database, const std::string& id, bool inlineView) {
    static const std::unordered_set<std::string> previewable = {
        "image/png", "image/jpeg", "image/gif", "image/webp", "image/bmp", "application/pdf"};

    FileRecord file;
    if (!database.getFile(id, file)) throw std::invalid_argument("The selected file does not exist.");
    const auto chunks = loadChunks(database, id);
    if (inlineView && !previewable.count(file.mimeType)) {
        throw std::invalid_argument("This file type cannot be previewed in the browser.");
    }

    std::string headers = "X-Content-Type-Options: nosniff\r\n";
    if (!file.fileSha256.empty()) headers += "X-Content-SHA256: " + file.fileSha256 + "\r\n";
    if (inlineView) {
        headers += "Content-Disposition: inline\r\n";
        if (file.mimeType != "application/pdf") headers += "Content-Security-Policy: sandbox\r\n";
    } else {
        headers += "Content-Disposition: attachment; filename=\"" + asciiName(file.fileName) +
                   "\"; filename*=UTF-8''" + urlEncode(file.fileName) + "\r\n";
    }

    bool started = false;
    storage::RestoreResult result;
    try {
        result = storage::reconstruct(chunks, [&](const char* data, size_t size) -> bool {
            if (!started) {
                started = true;
                if (!sendHead(socket, 200, file.mimeType, file.originalSize, headers)) return false;
            }
            return sendBytes(socket, data, size);
        });
    } catch (...) {
        if (started) return;
        throw;
    }
    if (result.repaired > 0) {
        database.logActivity("RECOVERY", id + ": restored " + to_string(result.repaired) + " copy/copies.");
    }
}

void handleFileRoute(int socket, const HttpRequest& request, const std::string& suffix) {
    const size_t slash = suffix.find('/');
    const std::string id = suffix.substr(0, slash);
    const std::string action = slash == std::string::npos ? "" : suffix.substr(slash + 1);
    if (id.empty()) throw std::invalid_argument("A file ID is required.");

    Database database;
    openDatabase(database);
    const std::string& m = request.method;

    if (m == "DELETE" && action.empty()) return respondJson(socket, 200, deleteStoredFile(database, id));
    if (m == "GET" && action == "chunks") {
        return respondJson(socket, 200, "{\"fileId\":" + quote(id) + ",\"chunks\":" + chunksJson(loadChunks(database, id, false)) + "}");
    }
    if (m == "POST" && action == "verify") return respondJson(socket, 200, verifyFile(database, id));
    if (m == "POST" && action == "recover") return respondJson(socket, 200, recoverFile(database, id));
    if (m == "GET" && action == "content") return respondJson(socket, 200, fileContentJson(database, id));
    if (m == "GET" && action == "download") return handleDownload(socket, database, id, parseForm(request.query)["inline"] == "1");
    respondError(socket, 404, "API endpoint not found.");
}

void serveStatic(int socket, const std::string& path) {
    static const std::unordered_map<std::string, std::pair<std::string, std::string>> pages = {
        {"/", {"index.html", "text/html; charset=utf-8"}},
        {"/index.html", {"index.html", "text/html; charset=utf-8"}},
        {"/style.css", {"style.css", "text/css; charset=utf-8"}},
        {"/script.js", {"script.js", "text/javascript; charset=utf-8"}}};

    const auto page = pages.find(path);
    if (page == pages.end()) return respondError(socket, 404, "Page not found.");
    std::ifstream input(fs::path("web") / page->second.first, std::ios::binary);
    if (!input) return respondError(socket, 404, "The web page files were not found. Start ChunkVault from its project folder.");
    std::ostringstream content;
    content << input.rdbuf();
    respond(socket, 200, page->second.second, content.str());
}

void handleApi(int socket, HttpRequest& request) {
    const std::string& m = request.method;
    const std::string& p = request.path;
    auto form = [&](size_t limit) {
        if (!receiveBody(socket, request, limit)) throw PayloadTooLarge("The request is too large.");
        return parseForm(request.body);
    };

    try {
        if (m == "GET" && (p == "/api/dashboard" || p == "/api/database" || p == "/api/files")) {
            Database database;
            openDatabase(database);
            if (p == "/api/dashboard") return respondJson(socket, 200, dashboardJson(database));
            if (p == "/api/database") return respondJson(socket, 200, databaseJson(database));
            std::vector<FileRecord> files;
            if (!database.getFiles(files)) throw std::runtime_error("Unable to read file metadata from MySQL.");
            return respondJson(socket, 200, filesJson(files));
        }

        if (m == "POST" && p == "/api/os/schedule") {
            const Form values = form(kFormLimit);
            const auto processes = processesFromForm(values);
            const auto algorithm = values.find("algorithm");
            if (algorithm == values.end()) throw std::invalid_argument("Choose a scheduling algorithm.");
            ScheduleResult result;
            std::string error;
            if (!calculateSchedule(processes, algorithm->second, formInt(values, "quantum", 0, kMaxInt), result, error)) {
                throw std::invalid_argument(error);
            }
            return respondJson(socket, 200, scheduleJson(result));
        }

        if (m == "POST" && p == "/api/os/deadlock") {
            const ResourceGraph graph = graphFromForm(form(kFormLimit));
            const auto cycle = detectDeadlockCycle(graph.processIds, graph.resources, graph.requests);
            return respondJson(socket, 200, std::string("{\"deadlocked\":") + (cycle.empty() ? "false" : "true") +
                                            ",\"cycle\":" + jsonArray(cycle, intJson) + "}");
        }

        if (m == "POST" && p == "/api/os/resources") {
            const Form values = form(kFormLimit);
            const ResourceGraph graph = graphFromForm(values);
            const auto action = values.find("action");
            if (action == values.end()) throw std::invalid_argument("Choose a resource action.");
            ResourceActionResult result;
            std::string error;
            if (!applyResourceAction(graph.processIds, graph.resources, graph.requests, action->second,
                                     formInt(values, "pid", 0, kMaxInt), formInt(values, "resourceId", 0, kMaxInt),
                                     result, error)) {
                throw std::invalid_argument(error);
            }
            return respondJson(socket, 200, resourceActionJson(result));
        }

        if (m == "POST" && p == "/api/activity") {
            Form values = form(65536);
            if (values["action"].empty() || values["details"].empty()) {
                throw std::invalid_argument("An action and description are required.");
            }
            Database database;
            if (!database.connect() || !database.logActivity(values["action"], values["details"])) {
                throw std::runtime_error("Unable to save the activity to MySQL.");
            }
            return respondJson(socket, 201, "{\"saved\":true}");
        }

        const std::string activityPrefix = "/api/activity/";
        if (m == "DELETE" && p.rfind(activityPrefix, 0) == 0) {
            const std::string raw = p.substr(activityPrefix.size());
            if (raw.empty() || raw.size() > 19 || raw.find_first_not_of("0123456789") != std::string::npos) {
                throw std::invalid_argument("A valid activity ID is required.");
            }
            Database database;
            if (!database.connect() || !database.deleteActivity(std::stoull(raw))) {
                throw std::runtime_error("Unable to delete the activity log from MySQL.");
            }
            return respondJson(socket, 200, "{\"deleted\":true}");
        }

        if (m == "POST" && p == "/api/files") return handleUpload(socket, request);

        const std::string filePrefix = "/api/files/";
        if (p.rfind(filePrefix, 0) == 0) return handleFileRoute(socket, request, p.substr(filePrefix.size()));

        respondError(socket, 404, "API endpoint not found.");
    } catch (const std::invalid_argument& error) {
        respondError(socket, 400, error.what());
    } catch (const PayloadTooLarge& error) {
        respondError(socket, 413, error.what());
    } catch (const std::exception& error) {
        respondError(socket, 500, error.what());
    }
}

void handleConnection(int socket) {
    HttpRequest request;
    if (!readRequest(socket, request)) return respondError(socket, 400, "The HTTP request could not be read.");

    const std::string origin = request.header("origin");
    if (!origin.empty() && origin != allowedOrigin) return respondError(socket, 403, "Requests from this origin are not allowed.");

    if (request.path.rfind("/api/", 0) == 0) handleApi(socket, request);
    else if (request.method == "GET") serveStatic(socket, request.path);
    else respondError(socket, 405, "This request method is not supported.");
}

}

int runWebServer(unsigned short port) {
    allowedOrigin = "http://127.0.0.1:" + std::to_string(port);
    std::signal(SIGPIPE, SIG_IGN);

    const int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        std::cerr << "Unable to create the web server socket.\n";
        return 1;
    }
    const int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(server, 16) < 0) {
        std::cerr << "Unable to start ChunkVault web server on port " << port << ".\n";
        close(server);
        return 1;
    }

    std::cout << "ChunkVault is available at http://127.0.0.1:" << port << "/\nPress Ctrl+C to stop the server.\n";
    for (;;) {
        const int client = accept(server, nullptr, nullptr);
        if (client < 0) {
            if (errno != EINTR) std::cerr << "Unable to accept a web request.\n";
            continue;
        }
        handleConnection(client);
        shutdown(client, SHUT_RDWR);
        close(client);
    }
}