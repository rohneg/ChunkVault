#ifndef CHUNKVAULT_HTTP_H
#define CHUNKVAULT_HTTP_H

#include <cstddef>
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
Form parseForm(const std::string& text);

bool readRequest(int socket, HttpRequest& request);
bool receiveBody(int socket, HttpRequest& request, std::size_t maximumSize);
bool streamBodyToFile(int socket, const HttpRequest& request, const std::filesystem::path& path);

void respond(int socket, int status, const std::string& contentType, const std::string& body);
void respondJson(int socket, int status, const std::string& body);
void respondError(int socket, int status, const std::string& message);

#endif