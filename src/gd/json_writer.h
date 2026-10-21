#pragma once
// JSON documents to write, as Godot's JSON.stringify(data, indent, sort_keys) writes them (the
// reading side is gd/json.h): ints as ints, floats with a fractional part (".0" when whole),
// objects with their keys sorted unless sort_keys is false, an indent string per level ("" writes
// everything on one line, as Godot does).
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gd/gd.h"

namespace gd {

class JsonValue {
public:
    enum class Type { Null, Bool, Int, Float, String, Array, Object };

    JsonValue() = default;
    JsonValue(bool b) : type_(Type::Bool), bool_(b) {}
    JsonValue(int i) : type_(Type::Int), int_(i) {}
    JsonValue(Int i) : type_(Type::Int), int_(i) {}
    // `digits`: significant digits written (17 keeps every double exactly).
    JsonValue(double d, int digits = 17) : type_(Type::Float), num_(d), digits_(digits) {}
    JsonValue(std::string s) : type_(Type::String), str_(std::move(s)) {}
    JsonValue(const char* s) : type_(Type::String), str_(s) {}
    static JsonValue array();
    static JsonValue object();

    Type type() const { return type_; }
    // Adds to an array / sets a key of an object (a key set again keeps its place).
    JsonValue& append(JsonValue v);
    JsonValue& set(const std::string& key, JsonValue v);

    bool boolean() const { return bool_; }
    Int integer() const { return int_; }
    double num() const { return num_; }
    int digits() const { return digits_; }
    const std::string& str() const { return str_; }
    const std::vector<JsonValue>& items() const;
    const std::vector<std::pair<std::string, JsonValue>>& members() const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    Int int_ = 0;
    double num_ = 0.0;
    int digits_ = 17;
    std::string str_;
    std::shared_ptr<std::vector<JsonValue>> arr_;
    std::shared_ptr<std::vector<std::pair<std::string, JsonValue>>> obj_;
};

// JSON.stringify(value, indent, sort_keys).
std::string json_stringify(const JsonValue& value, const std::string& indent = "", bool sort_keys = true);

}  // namespace gd
