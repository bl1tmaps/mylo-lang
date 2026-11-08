#include "json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const Json kNull;
const std::string kEmpty;

struct Parser {
    const char* p;
    const char* end;
    bool ok = true;

    void skipSpace() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }

    static void putUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
        }
    }

    unsigned hex4() {
        unsigned cp = 0;
        for (int i = 0; i < 4; ++i) {
            if (p >= end) { ok = false; return 0; }
            char c = *p++;
            cp <<= 4;
            if (c >= '0' && c <= '9') cp |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') cp |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') cp |= (unsigned)(c - 'A' + 10);
            else { ok = false; return 0; }
        }
        return cp;
    }

    std::string string() {
        std::string out;
        ++p;   // opening quote
        while (p < end && *p != '"') {
            char c = *p++;
            if (c != '\\') { out += c; continue; }
            if (p >= end) { ok = false; break; }
            char e = *p++;
            switch (e) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned cp = hex4();
                    // A surrogate pair encodes one character above U+FFFF.
                    if (cp >= 0xD800 && cp < 0xDC00 && p + 1 < end && p[0] == '\\' && p[1] == 'u') {
                        p += 2;
                        unsigned low = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    putUtf8(out, cp);
                    break;
                }
                default: out += e; break;
            }
        }
        if (p < end) ++p; else ok = false;
        return out;
    }

    Json value(int depth) {
        skipSpace();
        if (p >= end || depth > 200) { ok = false; return Json(); }
        char c = *p;
        if (c == '{') {
            ++p;
            Json obj = Json::object();
            skipSpace();
            if (p < end && *p == '}') { ++p; return obj; }
            while (ok && p < end) {
                skipSpace();
                if (p >= end || *p != '"') { ok = false; break; }
                std::string key = string();
                skipSpace();
                if (p >= end || *p != ':') { ok = false; break; }
                ++p;
                obj[key] = value(depth + 1);
                skipSpace();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == '}') { ++p; break; }
                ok = false;
            }
            return obj;
        }
        if (c == '[') {
            ++p;
            Json arr = Json::array();
            skipSpace();
            if (p < end && *p == ']') { ++p; return arr; }
            while (ok && p < end) {
                arr.push(value(depth + 1));
                skipSpace();
                if (p < end && *p == ',') { ++p; continue; }
                if (p < end && *p == ']') { ++p; break; }
                ok = false;
            }
            return arr;
        }
        if (c == '"') return Json(string());
        if (end - p >= 4 && std::strncmp(p, "true", 4) == 0) { p += 4; return Json(true); }
        if (end - p >= 5 && std::strncmp(p, "false", 5) == 0) { p += 5; return Json(false); }
        if (end - p >= 4 && std::strncmp(p, "null", 4) == 0) { p += 4; return Json(); }
        std::string number;
        while (p < end && (std::strchr("+-.eE", *p) || (*p >= '0' && *p <= '9'))) number += *p++;
        if (number.empty()) { ok = false; return Json(); }
        return Json(std::strtod(number.c_str(), nullptr));
    }
};

void dumpString(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    out += '"';
}

void dumpValue(std::string& out, const Json& v) {
    switch (v.type()) {
        case Json::Type::Null: out += "null"; break;
        case Json::Type::Bool: out += v.asBool() ? "true" : "false"; break;
        case Json::Type::Number: {
            double n = v.asNumber();
            char buf[32];
            if (std::isfinite(n) && n == std::floor(n) && std::fabs(n) < 9e15) std::snprintf(buf, sizeof(buf), "%lld", (long long)n);
            else if (std::isfinite(n)) std::snprintf(buf, sizeof(buf), "%.17g", n);
            else std::snprintf(buf, sizeof(buf), "null");
            out += buf;
            break;
        }
        case Json::Type::String: dumpString(out, v.asString()); break;
        case Json::Type::Array: {
            out += '[';
            bool first = true;
            for (const auto& item : v.items()) {
                if (!first) out += ',';
                first = false;
                dumpValue(out, item);
            }
            out += ']';
            break;
        }
        case Json::Type::Object: {
            out += '{';
            bool first = true;
            for (const auto& member : v.members()) {
                if (!first) out += ',';
                first = false;
                dumpString(out, member.first);
                out += ':';
                dumpValue(out, member.second);
            }
            out += '}';
            break;
        }
    }
}

} // namespace

Json Json::parse(const std::string& text, bool* ok) {
    Parser parser{text.data(), text.data() + text.size()};
    Json result = parser.value(0);
    if (ok) *ok = parser.ok;
    return parser.ok ? result : Json();
}

std::string Json::dump() const {
    std::string out;
    dumpValue(out, *this);
    return out;
}

const std::string& Json::asString() const { return m_type == Type::String ? m_string : kEmpty; }

const Json& Json::operator[](const std::string& key) const {
    if (m_type == Type::Object) {
        for (const auto& member : m_members) {
            if (member.first == key) return member.second;
        }
    }
    return kNull;
}

Json& Json::operator[](const std::string& key) {
    if (m_type != Type::Object) {
        *this = object();
    }
    for (auto& member : m_members) {
        if (member.first == key) return member.second;
    }
    m_members.emplace_back(key, Json());
    return m_members.back().second;
}

bool Json::has(const std::string& key) const {
    if (m_type != Type::Object) return false;
    for (const auto& member : m_members) {
        if (member.first == key) return true;
    }
    return false;
}

const Json& Json::operator[](size_t index) const {
    if (m_type == Type::Array && index < m_items.size()) return m_items[index];
    return kNull;
}

void Json::push(Json value) {
    if (m_type != Type::Array) *this = array();
    m_items.push_back(std::move(value));
}
