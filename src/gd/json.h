#pragma once
// JSON documents as Godot's JSON.parse_string returns them: every number is a float (double),
// arrays and objects are shared like Array / Dictionary, objects keep their key order.
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gd/gd.h"

namespace gd {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    static Json parse(const std::string& text, bool* ok = nullptr);

    Type type() const { return type_; }
    bool is_null() const { return type_ == Type::Null; }
    bool is_number() const { return type_ == Type::Number; }
    bool is_array() const { return type_ == Type::Array; }
    bool is_object() const { return type_ == Type::Object; }
    bool is_string() const { return type_ == Type::String; }
    bool is_bool() const { return type_ == Type::Bool; }

    double num() const { return num_; }
    bool boolean() const { return bool_; }
    const std::string& str() const { return str_; }

    // `var i: int = value` on a float: truncation toward zero (Variant float -> int).
    Int to_int() const { return type_ == Type::Number ? static_cast<Int>(num_) : (type_ == Type::Bool ? (bool_ ? 1 : 0) : 0); }

    Int size() const;
    const Json& operator[](Int i) const;
    const std::vector<Json>& items() const;
    bool has(const std::string& key) const;
    const Json& operator[](const std::string& key) const;   // missing key: fail()
    const Json& get(const std::string& key) const;          // missing key: a null value
    const std::vector<std::pair<std::string, Json>>& members() const;

private:
    friend class JsonParser;
    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::shared_ptr<std::vector<Json>> arr_;
    std::shared_ptr<std::vector<std::pair<std::string, Json>>> obj_;
};

}  // namespace gd
