#pragma once

#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <stdexcept>
#include <cmath>

namespace Prismix {
namespace Json {

enum class Type {
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object
};

class Value {
public:
    Type type = Type::Null;
    bool bool_val = false;
    double num_val = 0.0;
    std::string str_val;
    std::vector<Value> arr_val;
    std::vector<std::pair<std::string, Value>> obj_val;

    Value() : type(Type::Null) {}
    Value(std::nullptr_t) : type(Type::Null) {}
    Value(bool b) : type(Type::Boolean), bool_val(b) {}
    Value(int n) : type(Type::Number), num_val(n) {}
    Value(unsigned int n) : type(Type::Number), num_val(n) {}
    Value(int64_t n) : type(Type::Number), num_val(static_cast<double>(n)) {}
    Value(float n) : type(Type::Number), num_val(n) {}
    Value(double n) : type(Type::Number), num_val(n) {}
    Value(const char* s) : type(Type::String), str_val(s ? s : "") {}
    Value(const std::string& s) : type(Type::String), str_val(s) {}
    Value(std::string&& s) : type(Type::String), str_val(std::move(s)) {}

    static Value make_array() {
        Value v;
        v.type = Type::Array;
        return v;
    }

    static Value make_object() {
        Value v;
        v.type = Type::Object;
        return v;
    }

    bool is_null() const { return type == Type::Null; }
    bool is_bool() const { return type == Type::Boolean; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }

    size_t size() const {
        if (type == Type::Array) return arr_val.size();
        if (type == Type::Object) return obj_val.size();
        return 0;
    }

    // Array operations
    void push_back(const Value& v) {
        if (type != Type::Array) {
            type = Type::Array;
            arr_val.clear();
        }
        arr_val.push_back(v);
    }

    void push_back(Value&& v) {
        if (type != Type::Array) {
            type = Type::Array;
            arr_val.clear();
        }
        arr_val.push_back(std::move(v));
    }

    const Value& operator[](size_t idx) const {
        static Value s_null;
        if (type == Type::Array && idx < arr_val.size()) {
            return arr_val[idx];
        }
        return s_null;
    }

    Value& operator[](size_t idx) {
        if (type != Type::Array) {
            type = Type::Array;
        }
        if (idx >= arr_val.size()) {
            arr_val.resize(idx + 1);
        }
        return arr_val[idx];
    }

    // Object operations
    void set(const std::string& key, const Value& val) {
        if (type != Type::Object) {
            type = Type::Object;
            obj_val.clear();
        }
        for (auto& pair : obj_val) {
            if (pair.first == key) {
                pair.second = val;
                return;
            }
        }
        obj_val.emplace_back(key, val);
    }

    const Value& get(const std::string& key) const {
        static Value s_null;
        if (type == Type::Object) {
            for (const auto& pair : obj_val) {
                if (pair.first == key) return pair.second;
            }
        }
        return s_null;
    }

    bool has(const std::string& key) const {
        if (type == Type::Object) {
            for (const auto& pair : obj_val) {
                if (pair.first == key) return true;
            }
        }
        return false;
    }

    // Type getters with fallbacks
    std::string as_string(const std::string& def = "") const {
        if (type == Type::String) return str_val;
        if (type == Type::Number) {
            std::ostringstream ss;
            ss << num_val;
            return ss.str();
        }
        if (type == Type::Boolean) return bool_val ? "true" : "false";
        return def;
    }

    double as_double(double def = 0.0) const {
        if (type == Type::Number) return num_val;
        if (type == Type::String) {
            try { return std::stod(str_val); } catch (...) {}
        }
        if (type == Type::Boolean) return bool_val ? 1.0 : 0.0;
        return def;
    }

    float as_float(float def = 0.0f) const {
        return static_cast<float>(as_double(def));
    }

    int as_int(int def = 0) const {
        return static_cast<int>(as_double(def));
    }

    bool as_bool(bool def = false) const {
        if (type == Type::Boolean) return bool_val;
        if (type == Type::Number) return num_val != 0.0;
        if (type == Type::String) {
            return str_val == "true" || str_val == "1";
        }
        return def;
    }

    std::string get_string(const std::string& key, const std::string& def = "") const {
        return get(key).as_string(def);
    }

    float get_float(const std::string& key, float def = 0.0f) const {
        return get(key).as_float(def);
    }

    int get_int(const std::string& key, int def = 0) const {
        return get(key).as_int(def);
    }

    bool get_bool(const std::string& key, bool def = false) const {
        return get(key).as_bool(def);
    }

    // Serialization
    static std::string escape_string(const std::string& s) {
        std::ostringstream o;
        for (char c : s) {
            switch (c) {
                case '"':  o << "\\\""; break;
                case '\\': o << "\\\\"; break;
                case '\b': o << "\\b"; break;
                case '\f': o << "\\f"; break;
                case '\n': o << "\\n"; break;
                case '\r': o << "\\r"; break;
                case '\t': o << "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) <= 0x1f) {
                        o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                    } else {
                        o << c;
                    }
            }
        }
        return o.str();
    }

    std::string dump(int indent = -1, int current_indent = 0) const {
        std::ostringstream ss;
        const std::string ind(indent > 0 ? (current_indent * indent) : 0, ' ');
        const std::string child_ind(indent > 0 ? ((current_indent + 1) * indent) : 0, ' ');
        const std::string nl = indent > 0 ? "\n" : "";
        const std::string sp = indent > 0 ? " " : "";

        switch (type) {
            case Type::Null:
                ss << "null";
                break;
            case Type::Boolean:
                ss << (bool_val ? "true" : "false");
                break;
            case Type::Number: {
                if (std::isnan(num_val) || std::isinf(num_val)) {
                    ss << "0.0";
                } else if (std::floor(num_val) == num_val && std::abs(num_val) < 1e14) {
                    ss << static_cast<int64_t>(num_val);
                } else {
                    ss << std::setprecision(8) << num_val;
                }
                break;
            }
            case Type::String:
                ss << '"' << escape_string(str_val) << '"';
                break;
            case Type::Array: {
                if (arr_val.empty()) {
                    ss << "[]";
                } else {
                    ss << "[" << nl;
                    for (size_t i = 0; i < arr_val.size(); ++i) {
                        ss << child_ind << arr_val[i].dump(indent, current_indent + 1);
                        if (i + 1 < arr_val.size()) ss << ",";
                        ss << nl;
                    }
                    ss << ind << "]";
                }
                break;
            }
            case Type::Object: {
                if (obj_val.empty()) {
                    ss << "{}";
                } else {
                    ss << "{" << nl;
                    for (size_t i = 0; i < obj_val.size(); ++i) {
                        ss << child_ind << '"' << escape_string(obj_val[i].first) << "\":" << sp
                           << obj_val[i].second.dump(indent, current_indent + 1);
                        if (i + 1 < obj_val.size()) ss << ",";
                        ss << nl;
                    }
                    ss << ind << "}";
                }
                break;
            }
        }
        return ss.str();
    }
};

// Parser
class Parser {
    const std::string& src;
    size_t pos = 0;

    void skip_whitespace() {
        while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' || src[pos] == '\n' || src[pos] == '\r')) {
            ++pos;
        }
    }

    char peek() {
        skip_whitespace();
        return pos < src.size() ? src[pos] : '\0';
    }

    char get() {
        skip_whitespace();
        return pos < src.size() ? src[pos++] : '\0';
    }

    std::string parse_string() {
        if (get() != '"') return "";
        std::string s;
        while (pos < src.size()) {
            char c = src[pos++];
            if (c == '"') return s;
            if (c == '\\' && pos < src.size()) {
                char esc = src[pos++];
                switch (esc) {
                    case '"':  s += '"'; break;
                    case '\\': s += '\\'; break;
                    case '/':  s += '/'; break;
                    case 'b':  s += '\b'; break;
                    case 'f':  s += '\f'; break;
                    case 'n':  s += '\n'; break;
                    case 'r':  s += '\r'; break;
                    case 't':  s += '\t'; break;
                    case 'u': {
                        if (pos + 4 <= src.size()) {
                            // Simple ascii fallback for unicode escapes
                            pos += 4;
                            s += '?';
                        }
                        break;
                    }
                    default: s += esc; break;
                }
            } else {
                s += c;
            }
        }
        return s;
    }

    Value parse_number() {
        size_t start = pos;
        if (pos < src.size() && (src[pos] == '-' || src[pos] == '+')) ++pos;
        while (pos < src.size() && (std::isdigit(static_cast<unsigned char>(src[pos])) || src[pos] == '.' || src[pos] == 'e' || src[pos] == 'E' || src[pos] == '+' || src[pos] == '-')) {
            ++pos;
        }
        try {
            double v = std::stod(src.substr(start, pos - start));
            return Value(v);
        } catch (...) {
            return Value(0.0);
        }
    }

    Value parse_array() {
        get(); // consume '['
        Value arr = Value::make_array();
        if (peek() == ']') {
            get();
            return arr;
        }
        while (pos < src.size()) {
            arr.push_back(parse_value());
            char c = get();
            if (c == ']') return arr;
            if (c != ',') break;
        }
        return arr;
    }

    Value parse_object() {
        get(); // consume '{'
        Value obj = Value::make_object();
        if (peek() == '}') {
            get();
            return obj;
        }
        while (pos < src.size()) {
            if (peek() != '"') break;
            std::string key = parse_string();
            if (get() != ':') break;
            Value val = parse_value();
            obj.set(key, val);
            char c = get();
            if (c == '}') return obj;
            if (c != ',') break;
        }
        return obj;
    }

public:
    explicit Parser(const std::string& str) : src(str), pos(0) {}

    Value parse_value() {
        char c = peek();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return Value(parse_string());
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
        if (c == 't' || c == 'f') {
            std::string s;
            while (pos < src.size() && std::isalpha(static_cast<unsigned char>(src[pos]))) {
                s += src[pos++];
            }
            return Value(s == "true");
        }
        if (c == 'n') {
            pos += 4; // null
            return Value();
        }
        return Value();
    }
};

inline Value parse(const std::string& json_str) {
    Parser p(json_str);
    return p.parse_value();
}

} // namespace Json
} // namespace Prismix

