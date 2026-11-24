#ifndef CHUNKVAULT_JSON_H
#define CHUNKVAULT_JSON_H

#include <cstdio>
#include <string>

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