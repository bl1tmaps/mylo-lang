#pragma once

// A small JSON value type, enough for the Debug Adapter Protocol.

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : m_type(Type::Bool), m_bool(b) {}
    Json(int n) : m_type(Type::Number), m_number(n) {}
    Json(long long n) : m_type(Type::Number), m_number((double)n) {}
    Json(double n) : m_type(Type::Number), m_number(n) {}
    Json(const char* s) : m_type(Type::String), m_string(s) {}
    Json(std::string s) : m_type(Type::String), m_string(std::move(s)) {}

    static Json array() { Json j; j.m_type = Type::Array; return j; }
    static Json object() { Json j; j.m_type = Type::Object; return j; }

    // Parses text, returning a null value if it isn't valid JSON.
    static Json parse(const std::string& text, bool* ok = nullptr);
    std::string dump() const;

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }
    bool isObject() const { return m_type == Type::Object; }
    bool isArray() const { return m_type == Type::Array; }
    bool isString() const { return m_type == Type::String; }
    bool isNumber() const { return m_type == Type::Number; }

    bool asBool(bool fallback = false) const { return m_type == Type::Bool ? m_bool : fallback; }
    double asNumber(double fallback = 0.0) const { return m_type == Type::Number ? m_number : fallback; }
    int asInt(int fallback = 0) const { return m_type == Type::Number ? (int)m_number : fallback; }
    const std::string& asString() const;
    std::string asString(const std::string& fallback) const { return m_type == Type::String ? m_string : fallback; }

    // Object access. A missing key reads as null; writing one adds it.
    const Json& operator[](const std::string& key) const;
    Json& operator[](const std::string& key);
    bool has(const std::string& key) const;
    const std::vector<std::pair<std::string, Json>>& members() const { return m_members; }

    // Array access.
    size_t size() const { return m_type == Type::Array ? m_items.size() : m_members.size(); }
    const Json& operator[](size_t index) const;
    void push(Json value);
    const std::vector<Json>& items() const { return m_items; }

private:
    Type m_type = Type::Null;
    bool m_bool = false;
    double m_number = 0.0;
    std::string m_string;
    std::vector<Json> m_items;
    std::vector<std::pair<std::string, Json>> m_members;
};
