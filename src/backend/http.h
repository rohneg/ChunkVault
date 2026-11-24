#ifndef CHUNKVAULT_HTTP_H
#define CHUNKVAULT_HTTP_H
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <unordered_map>

using Form = std::unordered_map<std::string, std::string>;

struct HttpRequest {
    std::string method, path, query, body;
    std::unordered_map<std::string, std::string> headers;
    unsigned long long contentLength = 0;

    std::string header(const std::string& name) const {
        auto found = headers.find(name);
        return found == headers.end() ? "" : found->second;
    }
};
std::string urlDecode(const std::string& value);
std::string urlEncode(const std::string& value);
Form parseForm(const std::string& text);

bool readRequest(int socket, HttpRequest& request);
bool receiveBody(int socket, HttpRequest& request, std::size_t maximumSize);
bool streamBodyToFile(int socket, const HttpRequest& request, const std::filesystem::path& path);

void respond(int socket, int status, const std::string& contentType, const std::string& body,
             const std::string& extraHeaders = "");
void respondJson(int socket, int status, const std::string& body);
void respondError(int socket, int status, const std::string& message);

bool sendHead(int socket, int status, const std::string& contentType, unsigned long long length,
              const std::string& extraHeaders = "");
bool sendBytes(int socket, const char* data, std::size_t size);

enum class RangeResult { Full, Partial, Unsatisfiable };
RangeResult parseByteRange(const std::string& header, unsigned long long size,
                           unsigned long long& start, unsigned long long& end);

inline std::string jsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char encoded[7];
                    std::snprintf(encoded, sizeof(encoded), "\\u%04x", c);
                    out += encoded;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

inline std::string quote(const std::string& value) { return "\"" + jsonEscape(value) + "\""; }

template <class Range, class ToJson>
std::string jsonArray(const Range& items, ToJson toJson) {
    std::string out = "[";
    bool first = true;
    for (const auto& item : items) {
        if (!first) out += ',';
        first = false;
        out += toJson(item);
    }
    return out + ']';
}

#endif