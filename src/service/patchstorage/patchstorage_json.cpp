#include "patchstorage_json.h"
#include <cctype>
#include <charconv>
#include <cmath>
#include <sstream>
#include <iostream>

namespace prismix::patchstorage {

const std::string JsonValue::s_empty_str = "";

const JsonValue& JsonValue::null_instance() {
    static const JsonValue null_val;
    return null_val;
}

JsonValue::JsonValue() : type_(JsonType::Null) {}
JsonValue::JsonValue(bool val) : type_(JsonType::Bool), bool_val_(val) {}
JsonValue::JsonValue(int64_t val) : type_(JsonType::Number), num_val_(static_cast<double>(val)), int_val_(val) {}
JsonValue::JsonValue(double val) : type_(JsonType::Number), num_val_(val), int_val_(static_cast<int64_t>(val)) {}
JsonValue::JsonValue(std::string val) : type_(JsonType::String), str_val_(std::move(val)) {}
JsonValue::JsonValue(const char* val) : type_(JsonType::String), str_val_(val ? val : "") {}

bool JsonValue::as_bool(bool def) const {
    if (type_ == JsonType::Bool) return bool_val_;
    if (type_ == JsonType::Number) return int_val_ != 0;
    return def;
}

int64_t JsonValue::as_int64(int64_t def) const {
    if (type_ == JsonType::Number) return int_val_;
    return def;
}

int JsonValue::as_int(int def) const {
    if (type_ == JsonType::Number) return static_cast<int>(int_val_);
    return def;
}

double JsonValue::as_double(double def) const {
    if (type_ == JsonType::Number) return num_val_;
    return def;
}

const std::string& JsonValue::as_string(const std::string& def) const {
    if (type_ == JsonType::String) return str_val_;
    return def;
}

size_t JsonValue::size() const {
    if (type_ == JsonType::Array) return arr_val_.size();
    if (type_ == JsonType::Object) return obj_val_.size();
    return 0;
}

bool JsonValue::has_key(const std::string& key) const {
    if (type_ != JsonType::Object) return false;
    return obj_val_.find(key) != obj_val_.end();
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
    if (type_ != JsonType::Object) return null_instance();
    auto it = obj_val_.find(key);
    if (it != obj_val_.end()) return it->second;
    return null_instance();
}

const JsonValue& JsonValue::operator[](size_t index) const {
    if (type_ != JsonType::Array) return null_instance();
    if (index < arr_val_.size()) return arr_val_[index];
    return null_instance();
}

const std::vector<JsonValue>& JsonValue::as_array() const {
    static const std::vector<JsonValue> s_empty_arr;
    if (type_ == JsonType::Array) return arr_val_;
    return s_empty_arr;
}

const std::unordered_map<std::string, JsonValue>& JsonValue::as_object() const {
    static const std::unordered_map<std::string, JsonValue> s_empty_obj;
    if (type_ == JsonType::Object) return obj_val_;
    return s_empty_obj;
}

void JsonValue::add_element(JsonValue val) {
    if (type_ != JsonType::Array) {
        type_ = JsonType::Array;
        arr_val_.clear();
    }
    arr_val_.push_back(std::move(val));
}

void JsonValue::set_field(std::string key, JsonValue val) {
    if (type_ != JsonType::Object) {
        type_ = JsonType::Object;
        obj_val_.clear();
    }
    obj_val_[std::move(key)] = std::move(val);
}

// ---------------- Parser ----------------

namespace {

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text), pos_(0) {}

    JsonValue parse(std::string* err) {
        skip_whitespace();
        if (pos_ >= text_.size()) {
            if (err) *err = "Empty JSON input";
            return JsonValue::null_instance();
        }
        JsonValue root = parse_value(err);
        skip_whitespace();
        return root;
    }

private:
    void skip_whitespace() {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                pos_++;
            } else {
                break;
            }
        }
    }

    JsonValue parse_value(std::string* err) {
        skip_whitespace();
        if (pos_ >= text_.size()) {
            if (err) *err = "Unexpected end of input";
            return JsonValue::null_instance();
        }

        char c = text_[pos_];
        if (c == '{') return parse_object(err);
        if (c == '[') return parse_array(err);
        if (c == '"') return parse_string(err);
        if (c == 't' || c == 'f') return parse_bool(err);
        if (c == 'n') return parse_null(err);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number(err);

        if (err) *err = std::string("Unexpected character: ") + c;
        return JsonValue::null_instance();
    }

    JsonValue parse_object(std::string* err) {
        pos_++; // skip '{'
        JsonValue obj;
        // set type object directly
        obj.set_field("", JsonValue::null_instance());
        const_cast<std::unordered_map<std::string, JsonValue>&>(obj.as_object()).clear();

        while (pos_ < text_.size()) {
            skip_whitespace();
            if (pos_ >= text_.size()) break;
            if (text_[pos_] == '}') {
                pos_++;
                return obj;
            }

            if (text_[pos_] != '"') {
                if (err) *err = "Expected string key in object";
                return JsonValue::null_instance();
            }

            std::string key = parse_string(err).as_string();
            skip_whitespace();

            if (pos_ >= text_.size() || text_[pos_] != ':') {
                if (err) *err = "Expected ':' after object key";
                return JsonValue::null_instance();
            }
            pos_++; // skip ':'

            JsonValue val = parse_value(err);
            obj.set_field(std::move(key), std::move(val));

            skip_whitespace();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                pos_++;
            } else if (pos_ < text_.size() && text_[pos_] == '}') {
                pos_++;
                return obj;
            } else {
                if (err) *err = "Expected ',' or '}' in object";
                return JsonValue::null_instance();
            }
        }

        if (err) *err = "Unterminated object";
        return JsonValue::null_instance();
    }

    JsonValue parse_array(std::string* err) {
        pos_++; // skip '['
        JsonValue arr;
        // set type array
        arr.add_element(JsonValue::null_instance());
        const_cast<std::vector<JsonValue>&>(arr.as_array()).clear();

        while (pos_ < text_.size()) {
            skip_whitespace();
            if (pos_ >= text_.size()) break;
            if (text_[pos_] == ']') {
                pos_++;
                return arr;
            }

            JsonValue val = parse_value(err);
            arr.add_element(std::move(val));

            skip_whitespace();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                pos_++;
            } else if (pos_ < text_.size() && text_[pos_] == ']') {
                pos_++;
                return arr;
            } else {
                if (err) *err = "Expected ',' or ']' in array";
                return JsonValue::null_instance();
            }
        }

        if (err) *err = "Unterminated array";
        return JsonValue::null_instance();
    }

    JsonValue parse_string(std::string* err) {
        pos_++; // skip opening '"'
        std::string res;
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') {
                return JsonValue(std::move(res));
            }
            if (c == '\\') {
                if (pos_ >= text_.size()) {
                    if (err) *err = "Unexpected end of string escape";
                    return JsonValue::null_instance();
                }
                char esc = text_[pos_++];
                switch (esc) {
                    case '"': res.push_back('"'); break;
                    case '\\': res.push_back('\\'); break;
                    case '/': res.push_back('/'); break;
                    case 'b': res.push_back('\b'); break;
                    case 'f': res.push_back('\f'); break;
                    case 'n': res.push_back('\n'); break;
                    case 'r': res.push_back('\r'); break;
                    case 't': res.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 <= text_.size()) {
                            std::string hex_str(text_.substr(pos_, 4));
                            pos_ += 4;
                            try {
                                uint32_t cp = std::stoul(hex_str, nullptr, 16);
                                if (cp < 0x80) {
                                    res.push_back(static_cast<char>(cp));
                                } else if (cp < 0x800) {
                                    res.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
                                    res.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                                } else {
                                    res.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
                                    res.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                                    res.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                                }
                            } catch (...) {
                                res.push_back('?');
                            }
                        }
                        break;
                    }
                    default:
                        res.push_back(esc);
                        break;
                }
            } else {
                res.push_back(c);
            }
        }
        if (err) *err = "Unterminated string";
        return JsonValue::null_instance();
    }

    JsonValue parse_number(std::string* err) {
        size_t start = pos_;
        if (text_[pos_] == '-') pos_++;
        while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            pos_++;
        }
        bool is_float = false;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            is_float = true;
            pos_++;
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                pos_++;
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            is_float = true;
            pos_++;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                pos_++;
            }
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                pos_++;
            }
        }

        std::string_view num_str = text_.substr(start, pos_ - start);
        try {
            if (is_float) {
                double d = std::stod(std::string(num_str));
                return JsonValue(d);
            } else {
                int64_t n = std::stoll(std::string(num_str));
                return JsonValue(n);
            }
        } catch (...) {
            if (err) *err = "Invalid number format";
            return JsonValue::null_instance();
        }
    }

    JsonValue parse_bool(std::string* err) {
        if (text_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return JsonValue(true);
        }
        if (text_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return JsonValue(false);
        }
        if (err) *err = "Invalid boolean literal";
        return JsonValue::null_instance();
    }

    JsonValue parse_null(std::string* err) {
        if (text_.substr(pos_, 4) == "null") {
            pos_ += 4;
            return JsonValue::null_instance();
        }
        if (err) *err = "Invalid null literal";
        return JsonValue::null_instance();
    }

    std::string_view text_;
    size_t pos_{0};
};

PatchTarget parse_target_obj(const JsonValue& val) {
    PatchTarget t;
    t.id = val["id"].as_int();
    t.name = val["name"].as_string();
    t.slug = val["slug"].as_string();
    return t;
}

PatchFile parse_file_obj(const JsonValue& val) {
    PatchFile f;
    f.id = val["id"].as_int();
    f.filename = val["filename"].as_string();
    f.url = val["url"].as_string();
    f.filesize = static_cast<uint64_t>(val["filesize"].as_int64());
    if (val.has_key("target")) {
        f.target = parse_target_obj(val["target"]);
    }
    return f;
}

PatchStorageItem parse_item_obj(const JsonValue& val) {
    PatchStorageItem item;
    item.id = val["id"].as_int();
    item.url = val["url"].as_string();
    item.created_at = val["created_at"].as_string();
    item.updated_at = val["updated_at"].as_string();
    item.slug = val["slug"].as_string();
    item.title = val["title"].as_string();
    // Sometimes WordPress returns title as object {"rendered": "..."}
    if (item.title.empty() && val["title"].is_object()) {
        item.title = val["title"]["rendered"].as_string();
    }
    item.excerpt = val["excerpt"].as_string();
    if (item.excerpt.empty() && val["excerpt"].is_object()) {
        item.excerpt = val["excerpt"]["rendered"].as_string();
    }
    item.content = val["content"].as_string();
    if (item.content.empty() && val["content"].is_object()) {
        item.content = val["content"]["rendered"].as_string();
    }
    item.revision = val["revision"].as_string();
    item.source_code_url = val["source_code_url"].as_string();

    if (val.has_key("artwork") && val["artwork"].is_object()) {
        item.artwork_url = val["artwork"]["url"].as_string();
        item.artwork_thumbnail_url = val["artwork"]["thumbnail_url"].as_string();
    }

    if (val.has_key("author") && val["author"].is_object()) {
        const auto& auth = val["author"];
        item.author.id = auth["id"].as_int();
        item.author.name = auth["name"].as_string();
        item.author.slug = auth["slug"].as_string();
        item.author.avatar_url = auth["avatar_url"].as_string();
    }

    if (val.has_key("license")) {
        if (val["license"].is_object()) {
            item.license_name = val["license"]["name"].as_string();
        } else if (val["license"].is_string()) {
            item.license_name = val["license"].as_string();
        }
    }

    if (val.has_key("uids") && val["uids"].is_array()) {
        for (const auto& u : val["uids"].as_array()) {
            item.uids.push_back(u.as_string());
        }
    }

    if (val.has_key("categories") && val["categories"].is_array()) {
        for (const auto& cat : val["categories"].as_array()) {
            PatchCategory c;
            c.id = cat["id"].as_int();
            c.name = cat["name"].as_string();
            c.slug = cat["slug"].as_string();
            item.categories.push_back(std::move(c));
        }
    }

    if (val.has_key("targets") && val["targets"].is_array()) {
        for (const auto& tgt : val["targets"].as_array()) {
            item.targets.push_back(parse_target_obj(tgt));
        }
    }

    if (val.has_key("files") && val["files"].is_array()) {
        for (const auto& f : val["files"].as_array()) {
            item.files.push_back(parse_file_obj(f));
        }
    }

    return item;
}

} // namespace

JsonValue parse_json(std::string_view text, std::string* out_error) {
    Parser p(text);
    return p.parse(out_error);
}

bool parse_patches_list(std::string_view json_str,
                        std::vector<PatchStorageItem>& out_items,
                        std::string* out_error) {
    out_items.clear();
    JsonValue root = parse_json(json_str, out_error);
    if (!root.is_array()) {
        if (out_error && out_error->empty()) {
            *out_error = "Root response is not an array of patches";
        }
        return false;
    }

    for (const auto& item_val : root.as_array()) {
        if (item_val.is_object()) {
            out_items.push_back(parse_item_obj(item_val));
        }
    }
    return true;
}

bool parse_single_patch(std::string_view json_str,
                        PatchStorageItem& out_item,
                        std::string* out_error) {
    JsonValue root = parse_json(json_str, out_error);
    if (!root.is_object()) {
        if (out_error && out_error->empty()) {
            *out_error = "Root response is not a patch object";
        }
        return false;
    }
    out_item = parse_item_obj(root);
    return true;
}

bool parse_platform_targets(std::string_view json_str,
                            std::vector<PatchTarget>& out_targets,
                            std::string* out_error) {
    out_targets.clear();
    JsonValue root = parse_json(json_str, out_error);
    if (!root.is_object()) {
        if (out_error && out_error->empty()) {
            *out_error = "Root response is not a platform object";
        }
        return false;
    }

    if (root.has_key("targets") && root["targets"].is_array()) {
        for (const auto& tgt : root["targets"].as_array()) {
            out_targets.push_back(parse_target_obj(tgt));
        }
        return true;
    }

    return false;
}

bool parse_categories_list(std::string_view json_str,
                           std::vector<PatchCategory>& out_categories,
                           std::string* out_error) {
    out_categories.clear();
    JsonValue root = parse_json(json_str, out_error);
    if (!root.is_array()) {
        if (out_error && out_error->empty()) {
            *out_error = "Root response is not a category array";
        }
        return false;
    }

    for (const auto& cat : root.as_array()) {
        if (!cat.is_object()) continue;
        PatchCategory c;
        c.id = cat["id"].as_int();
        c.name = cat["name"].as_string();
        c.slug = cat["slug"].as_string();
        if (c.id == 0 && c.name.empty()) continue;
        out_categories.push_back(std::move(c));
    }
    return true;
}

} // namespace prismix::patchstorage

