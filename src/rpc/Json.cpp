#include "Json.h"
#include <sstream>
#include <cstdio>
#include <cctype>
#include <stdexcept>
#include <cmath>
#include <limits>

namespace auracash {
namespace json {

static void dump_internal(const Value& v, std::ostringstream& out);

std::string Value::dump() const {
    std::ostringstream out;
    dump_internal(*this, out);
    return out.str();
}

static void dump_internal(const Value& v, std::ostringstream& out) {
    if (v.is_null()) {
        out << "null";
    } else if (v.is_bool()) {
        out << (v.as_bool() ? "true" : "false");
    } else if (v.is_int64()) {
        out << v.as_int64();
    } else if (v.is_uint64()) {
        out << v.as_uint64();
    } else if (v.is_number()) {
        out << v.as_number();
    } else if (v.is_string()) {
        out << '"' << escape(v.as_string()) << '"';
    } else if (v.is_array()) {
        out << '[';
        bool first = true;
        for (const auto& item : v.as_array()) {
            if (!first) out << ',';
            first = false;
            dump_internal(item, out);
        }
        out << ']';
    } else if (v.is_object()) {
        out << '{';
        bool first = true;
        for (const auto& kv : v.as_object()) {
            if (!first) out << ',';
            first = false;
            out << '"' << escape(kv.first) << "\":";
            dump_internal(kv.second, out);
        }
        out << '}';
    }
}

std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

class Parser {
public:
    explicit Parser(std::string_view text) : m_text(text) {}

    Value parse() {
        skip_ws();
        Value v = parse_value();
        skip_ws();
        return v;
    }

private:
    std::string_view m_text;
    size_t pos = 0;

    void skip_ws() {
        while (pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[pos]))) ++pos;
    }

    char peek() const {
        return pos < m_text.size() ? m_text[pos] : '\0';
    }

    char get() {
        return pos < m_text.size() ? m_text[pos++] : '\0';
    }

    Value parse_value() {
        char c = peek();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return parse_string();
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
        throw std::runtime_error("Unexpected character in JSON");
    }

    Value parse_object() {
        get(); // '{'
        Value::Object obj;
        skip_ws();
        if (peek() == '}') { get(); return Value(std::move(obj)); }
        while (true) {
            skip_ws();
            Value key = parse_string();
            skip_ws();
            if (get() != ':') throw std::runtime_error("Expected ':' in object");
            skip_ws();
            Value val = parse_value();
            obj[key.as_string()] = std::move(val);
            skip_ws();
            char c = get();
            if (c == '}') break;
            if (c != ',') throw std::runtime_error("Expected ',' or '}' in object");
        }
        return Value(std::move(obj));
    }

    Value parse_array() {
        get(); // '['
        Value::Array arr;
        skip_ws();
        if (peek() == ']') { get(); return Value(std::move(arr)); }
        while (true) {
            skip_ws();
            arr.push_back(parse_value());
            skip_ws();
            char c = get();
            if (c == ']') break;
            if (c != ',') throw std::runtime_error("Expected ',' or ']' in array");
        }
        return Value(std::move(arr));
    }

    Value parse_string() {
        get(); // '"'
        std::string s;
        while (true) {
            if (pos >= m_text.size()) throw std::runtime_error("Unterminated string");
            char c = get();
            if (c == '"') break;
            if (c == '\\') {
                char esc = get();
                switch (esc) {
                    case '"':  s += '"';  break;
                    case '\\': s += '\\'; break;
                    case '/':  s += '/';  break;
                    case 'b':  s += '\b'; break;
                    case 'f':  s += '\f'; break;
                    case 'n':  s += '\n'; break;
                    case 'r':  s += '\r'; break;
                    case 't':  s += '\t'; break;
                    case 'u': {
                        if (pos + 4 > m_text.size()) throw std::runtime_error("Invalid unicode escape");
                        unsigned int codepoint = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = get();
                            codepoint = codepoint * 16 + hex_val(h);
                        }
                        if (codepoint <= 0x7F) s += static_cast<char>(codepoint);
                        else if (codepoint <= 0x7FF) {
                            s += static_cast<char>(0xC0 | (codepoint >> 6));
                            s += static_cast<char>(0x80 | (codepoint & 0x3F));
                        } else {
                            s += static_cast<char>(0xE0 | (codepoint >> 12));
                            s += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                            s += static_cast<char>(0x80 | (codepoint & 0x3F));
                        }
                        break;
                    }
                    default: throw std::runtime_error("Unknown escape");
                }
            } else {
                s += c;
            }
        }
        return Value(s);
    }

    Value parse_number() {
        size_t start = pos;
        if (peek() == '-') get();
        while (std::isdigit(static_cast<unsigned char>(peek()))) get();
        bool is_float = false;
        if (peek() == '.') {
            is_float = true;
            get();
            while (std::isdigit(static_cast<unsigned char>(peek()))) get();
        }
        if (peek() == 'e' || peek() == 'E') {
            is_float = true;
            get();
            if (peek() == '+' || peek() == '-') get();
            while (std::isdigit(static_cast<unsigned char>(peek()))) get();
        }
        std::string_view sv = m_text.substr(start, pos - start);
        if (is_float) {
            double d = std::stod(std::string(sv));
            return Value(d);
        } else {
            std::string str(sv);
            if (!str.empty() && str[0] == '-') {
                int64_t i = std::stoll(str);
                return Value(i);
            }
            // Unsigned path: try uint64 if > INT64_MAX
            if (str.size() > 18) {
                uint64_t u = std::stoull(str);
                return Value(u);
            }
            uint64_t u = std::stoull(str);
            if (u > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                return Value(u);
            }
            // Prefer signed if fits, to keep JSON small
            int64_t i = static_cast<int64_t>(u);
            return Value(i);
        }
    }

    Value parse_bool() {
        if (m_text.substr(pos, 4) == "true") { pos += 4; return Value(true); }
        if (m_text.substr(pos, 5) == "false") { pos += 5; return Value(false); }
        throw std::runtime_error("Invalid boolean");
    }

    Value parse_null() {
        if (m_text.substr(pos, 4) == "null") { pos += 4; return Value(nullptr); }
        throw std::runtime_error("Invalid null");
    }

    static int hex_val(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw std::runtime_error("Invalid hex digit");
    }
};

Value parse(const std::string& text) {
    Parser p(text);
    return p.parse();
}

} // namespace json
} // namespace auracash
