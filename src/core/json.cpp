#include "core/json.h"

#include <charconv>
#include <cstdlib>

namespace blueward {

Json Json::Bool(bool v) {
    Json j;
    j.type_ = Type::Bool;
    j.bool_ = v;
    return j;
}

Json Json::Int(std::int64_t v) {
    Json j;
    j.type_ = Type::Number;
    j.integer_ = true;
    j.int_ = v;
    return j;
}

Json Json::String(std::string v) {
    Json j;
    j.type_ = Type::String;
    j.string_ = std::move(v);
    return j;
}

Json Json::Array() {
    Json j;
    j.type_ = Type::Array;
    return j;
}

Json Json::Object() {
    Json j;
    j.type_ = Type::Object;
    return j;
}

const Json* Json::Find(std::string_view key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& member : object_)
        if (member.first == key) return &member.second;
    return nullptr;
}

void Json::Set(std::string key, Json v) {
    if (type_ != Type::Object) *this = Object();
    for (auto& member : object_) {
        if (member.first == key) {
            member.second = std::move(v);
            return;
        }
    }
    object_.emplace_back(std::move(key), std::move(v));
}

void Json::Erase(std::string_view key) {
    if (type_ != Type::Object) return;
    for (auto it = object_.begin(); it != object_.end(); ++it) {
        if (it->first == key) {
            object_.erase(it);
            return;
        }
    }
}

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : t_(text) {}

    bool Parse(Json& out, std::string& error) {
        if (t_.size() >= 3 && static_cast<unsigned char>(t_[0]) == 0xEF && static_cast<unsigned char>(t_[1]) == 0xBB &&
            static_cast<unsigned char>(t_[2]) == 0xBF)
            pos_ = 3;
        SkipSpace();
        if (!Value(out, 0)) {
            error = err_;
            return false;
        }
        SkipSpace();
        if (pos_ != t_.size()) {
            Fail("unexpected text after the value");
            error = err_;
            return false;
        }
        return true;
    }

private:
    bool Fail(const char* message) {
        if (err_.empty()) err_ = std::string(message) + " at byte " + std::to_string(pos_);
        return false;
    }

    void SkipSpace() {
        while (pos_ < t_.size() && (t_[pos_] == ' ' || t_[pos_] == '\t' || t_[pos_] == '\n' || t_[pos_] == '\r')) ++pos_;
    }

    bool Literal(std::string_view word, Json value, Json& out) {
        if (t_.substr(pos_, word.size()) != word) return Fail("invalid literal");
        pos_ += word.size();
        out = std::move(value);
        return true;
    }

    bool Value(Json& out, int depth) {
        if (depth >= 64) return Fail("nested too deeply");
        if (pos_ >= t_.size()) return Fail("unexpected end of input");
        switch (t_[pos_]) {
            case '{': return ObjectValue(out, depth);
            case '[': return ArrayValue(out, depth);
            case '"': {
                std::string s;
                if (!StringValue(s)) return false;
                out = Json::String(std::move(s));
                return true;
            }
            case 't': return Literal("true", Json::Bool(true), out);
            case 'f': return Literal("false", Json::Bool(false), out);
            case 'n': return Literal("null", Json(), out);
            default: return NumberValue(out);
        }
    }

    bool NumberValue(Json& out) {
        const std::size_t start = pos_;
        bool whole = true;
        if (pos_ < t_.size() && t_[pos_] == '-') ++pos_;
        const std::size_t digits = pos_;
        while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9') ++pos_;
        if (pos_ == digits) return Fail("invalid value");
        if (t_[digits] == '0' && pos_ - digits > 1) return Fail("leading zero in a number");
        if (pos_ < t_.size() && t_[pos_] == '.') {
            whole = false;
            ++pos_;
            const std::size_t fraction = pos_;
            while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9') ++pos_;
            if (pos_ == fraction) return Fail("missing digits after the decimal point");
        }
        if (pos_ < t_.size() && (t_[pos_] == 'e' || t_[pos_] == 'E')) {
            whole = false;
            ++pos_;
            if (pos_ < t_.size() && (t_[pos_] == '+' || t_[pos_] == '-')) ++pos_;
            const std::size_t exponent = pos_;
            while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9') ++pos_;
            if (pos_ == exponent) return Fail("missing digits in the exponent");
        }
        const std::string text(t_.substr(start, pos_ - start));
        if (whole) {
            std::int64_t v = 0;
            const auto result = std::from_chars(text.data(), text.data() + text.size(), v);
            if (result.ec == std::errc() && result.ptr == text.data() + text.size()) {
                out = Json::Int(v);
                return true;
            }
        }
        out = Json();
        out.type_ = Json::Type::Number;
        out.double_ = std::strtod(text.c_str(), nullptr);
        return true;
    }

    static void AppendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool Hex4(unsigned& value) {
        if (pos_ + 4 > t_.size()) return Fail("short \\u escape");
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = t_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else return Fail("bad hex digit in a \\u escape");
        }
        return true;
    }

    bool StringValue(std::string& out) {
        ++pos_;
        while (pos_ < t_.size()) {
            const char c = t_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return Fail("raw control character in a string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= t_.size()) break;
            const char e = t_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!Hex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        unsigned low = 0;
                        if (t_.substr(pos_, 2) != "\\u") return Fail("lone high surrogate");
                        pos_ += 2;
                        if (!Hex4(low)) return false;
                        if (low < 0xDC00 || low > 0xDFFF) return Fail("bad low surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return Fail("lone low surrogate");
                    }
                    AppendUtf8(out, cp);
                    break;
                }
                default: return Fail("unknown escape");
            }
        }
        return Fail("unterminated string");
    }

    bool ArrayValue(Json& out, int depth) {
        ++pos_;
        out = Json::Array();
        SkipSpace();
        if (pos_ < t_.size() && t_[pos_] == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            Json item;
            SkipSpace();
            if (!Value(item, depth + 1)) return false;
            out.Push(std::move(item));
            SkipSpace();
            if (pos_ >= t_.size()) return Fail("unterminated array");
            if (t_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (t_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return Fail("expected , or ] in an array");
        }
    }

    bool ObjectValue(Json& out, int depth) {
        ++pos_;
        out = Json::Object();
        SkipSpace();
        if (pos_ < t_.size() && t_[pos_] == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            SkipSpace();
            if (pos_ >= t_.size() || t_[pos_] != '"') return Fail("expected a key");
            std::string key;
            if (!StringValue(key)) return false;
            SkipSpace();
            if (pos_ >= t_.size() || t_[pos_] != ':') return Fail("expected : after a key");
            ++pos_;
            SkipSpace();
            Json item;
            if (!Value(item, depth + 1)) return false;
            out.object_.emplace_back(std::move(key), std::move(item));
            SkipSpace();
            if (pos_ >= t_.size()) return Fail("unterminated object");
            if (t_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (t_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return Fail("expected , or } in an object");
        }
    }

    std::string_view t_;
    std::size_t pos_ = 0;
    std::string err_;
};

bool ParseJson(std::string_view text, Json& out, std::string& error) {
    error.clear();
    return JsonParser(text).Parse(out, error);
}

namespace {

void AppendEscaped(std::string& out, const std::string& s) {
    out += '"';
    for (const char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char* const kHex = "0123456789abcdef";
                    out += "\\u00";
                    out += kHex[(c >> 4) & 15];
                    out += kHex[c & 15];
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void Dump(const Json& v, std::string& out, int level) {
    const std::string pad(static_cast<std::size_t>(level + 1) * 2, ' '), close(static_cast<std::size_t>(level) * 2, ' ');
    switch (v.type()) {
        case Json::Type::Null: out += "null"; break;
        case Json::Type::Bool: out += v.asBool() ? "true" : "false"; break;
        case Json::Type::Number:
            if (v.isInteger()) {
                out += std::to_string(v.asInt());
            } else {
                char buffer[40];
                const auto result = std::to_chars(buffer, buffer + sizeof buffer, v.asDouble());
                out.append(buffer, result.ptr);
            }
            break;
        case Json::Type::String: AppendEscaped(out, v.asString()); break;
        case Json::Type::Array:
            if (v.items().empty()) {
                out += "[]";
                break;
            }
            out += "[\n";
            for (std::size_t i = 0; i < v.items().size(); ++i) {
                out += pad;
                Dump(v.items()[i], out, level + 1);
                out += i + 1 < v.items().size() ? ",\n" : "\n";
            }
            out += close + "]";
            break;
        case Json::Type::Object:
            if (v.members().empty()) {
                out += "{}";
                break;
            }
            out += "{\n";
            for (std::size_t i = 0; i < v.members().size(); ++i) {
                out += pad;
                AppendEscaped(out, v.members()[i].first);
                out += ": ";
                Dump(v.members()[i].second, out, level + 1);
                out += i + 1 < v.members().size() ? ",\n" : "\n";
            }
            out += close + "}";
            break;
    }
}

}  // namespace

std::string DumpJson(const Json& value) {
    std::string out;
    Dump(value, out, 0);
    out += '\n';
    return out;
}

}  // namespace blueward
