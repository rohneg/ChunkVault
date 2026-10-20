#include "http.h"
#include "json.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/socket.h>

namespace fs = std::filesystem;

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string trim(const std::string& value) {
    const size_t begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    return value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
}

bool sendAll(int socket, const char* data, size_t size) {
    for (size_t sent = 0; sent < size;) {
        const ssize_t count = send(socket, data + sent, size - sent, 0);
        if (count <= 0) return false;
        sent += static_cast<size_t>(count);
    }
    return true;
}

const char* reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        default: return "Internal Server Error";
    }
}

}

std::string urlDecode(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            out += ' ';
        } else if (value[i] == '%' && i + 2 < value.size()) {
            const char encoded[3] = {value[i + 1], value[i + 2], '\0'};
            char* end = nullptr;
            const long decoded = std::strtol(encoded, &end, 16);
            if (end != encoded + 2) return "";
            out += static_cast<char>(decoded);
            i += 2;
        } else {
            out += value[i];
        }
    }
    return out;
}

Form parseForm(const std::string& text) {
    Form values;
    for (size_t start = 0; start <= text.size();) {
        const size_t end = text.find('&', start);
        const std::string item = text.substr(start, end == std::string::npos ? end : end - start);
        const size_t equal = item.find('=');
        const std::string key = urlDecode(item.substr(0, equal));
        if (!key.empty()) values[key] = equal == std::string::npos ? "" : urlDecode(item.substr(equal + 1));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return values;
}

bool readRequest(int socket, HttpRequest& request) {
    std::string received;
    std::array<char, 8192> buffer;
    size_t headerEnd;
    while ((headerEnd = received.find("\r\n\r\n")) == std::string::npos) {
        const ssize_t count = recv(socket, buffer.data(), buffer.size(), 0);
        if (count <= 0) return false;
        received.append(buffer.data(), static_cast<size_t>(count));
        if (received.size() > 32768) return false;
    }

    std::istringstream head(received.substr(0, headerEnd));
    std::string line, target, version;
    std::getline(head, line);
    std::istringstream requestLine(line);
    if (!(requestLine >> request.method >> target >> version)) return false;

    while (std::getline(head, line)) {
        const size_t colon = line.find(':');
        if (colon != std::string::npos) request.headers[lower(trim(line.substr(0, colon)))] = trim(line.substr(colon + 1));
    }

    const size_t mark = target.find('?');
    request.path = target.substr(0, mark);
    request.query = mark == std::string::npos ? "" : target.substr(mark + 1);

    const std::string length = request.header("content-length");
    if (!length.empty()) {
        try { request.contentLength = std::stoull(length); } catch (...) { return false; }
    }
    request.body = received.substr(headerEnd + 4);
    if (request.body.size() > request.contentLength) request.body.resize(static_cast<size_t>(request.contentLength));
    return true;
}

bool receiveBody(int socket, HttpRequest& request, size_t maximumSize) {
    if (request.contentLength > maximumSize) return false;
    std::array<char, 8192> buffer;
    while (request.body.size() < request.contentLength) {
        const size_t remaining = static_cast<size_t>(request.contentLength) - request.body.size();
        const ssize_t count = recv(socket, buffer.data(), std::min(buffer.size(), remaining), 0);
        if (count <= 0) return false;
        request.body.append(buffer.data(), static_cast<size_t>(count));
    }
    return true;
}

bool streamBodyToFile(int socket, const HttpRequest& request, const fs::path& path) {
    if (request.contentLength == 0) return false;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;

    unsigned long long written = std::min<unsigned long long>(request.body.size(), request.contentLength);
    output.write(request.body.data(), static_cast<std::streamsize>(written));
    bool ok = static_cast<bool>(output);

    std::array<char, 65536> buffer;
    while (ok && written < request.contentLength) {
        const size_t wanted = static_cast<size_t>(std::min<unsigned long long>(buffer.size(), request.contentLength - written));
        const ssize_t count = recv(socket, buffer.data(), wanted, 0);
        if (count <= 0) { ok = false; break; }
        output.write(buffer.data(), count);
        ok = static_cast<bool>(output);
        written += static_cast<unsigned long long>(count);
    }
    output.close();
    if (!ok || !output) {
        std::error_code ignored;
        fs::remove(path, ignored);
        return false;
    }
    return true;
}

void respond(int socket, int status, const std::string& contentType, const std::string& body) {
    const std::string head = "HTTP/1.1 " + std::to_string(status) + ' ' + reason(status) +
        "\r\nContent-Type: " + contentType + "\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    if (sendAll(socket, head.data(), head.size())) sendAll(socket, body.data(), body.size());
}

void respondJson(int socket, int status, const std::string& body) {
    respond(socket, status, "application/json; charset=utf-8", body);
}

void respondError(int socket, int status, const std::string& message) {
    respondJson(socket, status, "{\"error\":" + quote(message) + "}");
}