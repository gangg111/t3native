#include "gd/json_writer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gd {

JsonValue JsonValue::array() {
    JsonValue v;
    v.type_ = Type::Array;
    v.arr_ = std::make_shared<std::vector<JsonValue>>();
    return v;
}

JsonValue JsonValue::object() {
    JsonValue v;
    v.type_ = Type::Object;
    v.obj_ = std::make_shared<std::vector<std::pair<std::string, JsonValue>>>();
    return v;
}

JsonValue& JsonValue::append(JsonValue v) {
    if (type_ != Type::Array) fail("JsonValue: append on a value that is no array");
    arr_->push_back(std::move(v));
    return *this;
}

JsonValue& JsonValue::set(const std::string& key, JsonValue v) {
    if (type_ != Type::Object) fail("JsonValue: set on a value that is no object");
    for (auto& kv : *obj_) {
        if (kv.first == key) {
            kv.second = std::move(v);
            return *this;
        }
    }
    obj_->emplace_back(key, std::move(v));
    return *this;
}

const std::vector<JsonValue>& JsonValue::items() const {
    static const std::vector<JsonValue> empty;
    return type_ == Type::Array ? *arr_ : empty;
}

const std::vector<std::pair<std::string, JsonValue>>& JsonValue::members() const {
    static const std::vector<std::pair<std::string, JsonValue>> empty;
    return type_ == Type::Object ? *obj_ : empty;
}

namespace {

// String.json_escape.
std::string escaped(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\v': out += "\\v"; break;
            default: out.push_back(c);
        }
    }
    return out + "\"";
}

std::string number(double d, int digits) {
    if (std::isnan(d) || std::isinf(d)) return "null";   // JSON has no such numbers
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*g", digits, d);
    std::string out = buf;
    if (out.find_first_of(".eE") == std::string::npos) out += ".0";
    return out;
}

std::string indent_of(const std::string& indent, Int level) {
    std::string out;
    for (Int i = 0; i < level; ++i) out += indent;
    return out;
}

std::string stringify(const JsonValue& v, const std::string& indent, Int level, bool sort_keys) {
    const std::string end_statement = indent.empty() ? "" : "\n";
    const std::string colon = indent.empty() ? ":" : ": ";
    switch (v.type()) {
        case JsonValue::Type::Null: return "null";
        case JsonValue::Type::Bool: return v.boolean() ? "true" : "false";
        case JsonValue::Type::Int: return str(v.integer());
        case JsonValue::Type::Float: return number(v.num(), v.digits());
        case JsonValue::Type::String: return escaped(v.str());
        case JsonValue::Type::Array: {
            if (v.items().empty()) return "[]";
            std::string s = "[" + end_statement;
            bool first = true;
            for (const JsonValue& item : v.items()) {
                if (!first) s += "," + end_statement;
                first = false;
                s += indent_of(indent, level + 1) + stringify(item, indent, level + 1, sort_keys);
            }
            return s + end_statement + indent_of(indent, level) + "]";
        }
        case JsonValue::Type::Object: {
            if (v.members().empty()) return "{}";
            std::vector<const std::pair<std::string, JsonValue>*> members;
            for (const auto& kv : v.members()) members.push_back(&kv);
            if (sort_keys) {
                std::stable_sort(members.begin(), members.end(), [](auto* a, auto* b) { return a->first < b->first; });
            }
            std::string s = "{" + end_statement;
            bool first = true;
            for (const auto* kv : members) {
                if (!first) s += "," + end_statement;
                first = false;
                s += indent_of(indent, level + 1) + escaped(kv->first) + colon + stringify(kv->second, indent, level + 1, sort_keys);
            }
            return s + end_statement + indent_of(indent, level) + "}";
        }
    }
    return "null";
}

}  // namespace

std::string json_stringify(const JsonValue& value, const std::string& indent, bool sort_keys) {
    return stringify(value, indent, 0, sort_keys);
}

}  // namespace gd
