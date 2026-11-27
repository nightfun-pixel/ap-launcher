#pragma once
// A small JSON reader and writer. Objects keep their key order, whole numbers stay exact 64-bit
// integers, and the parser refuses input nested deeper than 64 levels.

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace blueward {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    static Json Bool(bool v);
    static Json Int(std::int64_t v);
    static Json String(std::string v);
    static Json Array();
    static Json Object();

    Type type() const { return type_; }
    bool isString() const { return type_ == Type::String; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }
    bool isInteger() const { return type_ == Type::Number && integer_; }

    bool asBool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
    const std::string& asString() const { return string_; }
    std::int64_t asInt(std::int64_t fallback = 0) const { return isInteger() ? int_ : fallback; }
    double asDouble(double fallback = 0) const {
        return type_ == Type::Number ? (integer_ ? static_cast<double>(int_) : double_) : fallback;
    }

    const std::vector<Json>& items() const { return array_; }
    const std::vector<std::pair<std::string, Json>>& members() const { return object_; }
    const Json* Find(std::string_view key) const;

    void Push(Json v) { array_.push_back(std::move(v)); }
    void Set(std::string key, Json v);
    void Erase(std::string_view key);

private:
    friend class JsonParser;
    Type type_ = Type::Null;
    bool bool_ = false, integer_ = false;
    std::int64_t int_ = 0;
    double double_ = 0;
    std::string string_;
    std::vector<Json> array_;
    std::vector<std::pair<std::string, Json>> object_;
};

// A leading UTF-8 byte order mark is skipped. On failure `error` says what and where.
bool ParseJson(std::string_view text, Json& out, std::string& error);
std::string DumpJson(const Json& value);

}  // namespace blueward
