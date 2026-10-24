#pragma once

#include "patchstorage_types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string_view>
#include <cstdint>

namespace prismix::patchstorage {

enum class JsonType {
    Null,
    Bool,
    Number,
    String,
    Array,
    Object
};

class JsonValue {
public:
    JsonValue();
    explicit JsonValue(bool val);
    explicit JsonValue(int64_t val);
    explicit JsonValue(double val);
    explicit JsonValue(std::string val);
    explicit JsonValue(const char* val);

    JsonType type() const { return type_; }
    bool is_null() const { return type_ == JsonType::Null; }
    bool is_bool() const { return type_ == JsonType::Bool; }
    bool is_number() const { return type_ == JsonType::Number; }
    bool is_string() const { return type_ == JsonType::String; }
    bool is_array() const { return type_ == JsonType::Array; }
    bool is_object() const { return type_ == JsonType::Object; }

    bool as_bool(bool def = false) const;
    int64_t as_int64(int64_t def = 0) const;
    int as_int(int def = 0) const;
    double as_double(double def = 0.0) const;
    const std::string& as_string(const std::string& def = s_empty_str) const;

    size_t size() const;
    bool has_key(const std::string& key) const;

    const JsonValue& operator[](const std::string& key) const;
    const JsonValue& operator[](size_t index) const;

    const std::vector<JsonValue>& as_array() const;
    const std::unordered_map<std::string, JsonValue>& as_object() const;

    void add_element(JsonValue val);
    void set_field(std::string key, JsonValue val);

    static const JsonValue& null_instance();

private:
    JsonType type_{JsonType::Null};
    bool bool_val_{false};
    double num_val_{0.0};
    int64_t int_val_{0};
    std::string str_val_;
    std::vector<JsonValue> arr_val_;
    std::unordered_map<std::string, JsonValue> obj_val_;

    static const std::string s_empty_str;
};

JsonValue parse_json(std::string_view text, std::string* out_error = nullptr);

bool parse_patches_list(std::string_view json_str,
                        std::vector<PatchStorageItem>& out_items,
                        std::string* out_error = nullptr);

bool parse_single_patch(std::string_view json_str,
                        PatchStorageItem& out_item,
                        std::string* out_error = nullptr);

bool parse_platform_targets(std::string_view json_str,
                            std::vector<PatchTarget>& out_targets,
                            std::string* out_error = nullptr);

bool parse_categories_list(std::string_view json_str,
                           std::vector<PatchCategory>& out_categories,
                           std::string* out_error = nullptr);

} // namespace prismix::patchstorage

