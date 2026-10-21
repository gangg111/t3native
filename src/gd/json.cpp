#include "gd/json.h"

#include <cstdlib>

namespace gd {

namespace {
const Json& null_json() {
    static const Json value;
    return value;
}
}  // namespace

class JsonParser {
public:
    explicit JsonParser(const std::string& s) : s_(s) {}

    bool run(Json& out) {
        skip();
        if (!value(out)) return false;
        skip();
        return pos_ == s_.size();
    }

private:
    void skip() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t')) ++pos_;
    }
    bool literal(const char* word) {
        size_t n = std::strlen(word);
        if (s_.compare(pos_, n, word) != 0) return false;
        pos_ += n;
        return true;
    }
    bool value(Json& out) {
        if (pos_ >= s_.size()) return false;
        char c = s_[pos_];
        if (c == '{') return object(out);
        if (c == '[') return array(out);
        if (c == '"') { out.type_ = Json::Type::String; return string(out.str_); }
        if (c == 't') { out.type_ = Json::Type::Bool; out.bool_ = true; return literal("true"); }
        if (c == 'f') { out.type_ = Json::Type::Bool; out.bool_ = false; return literal("false"); }
        if (c == 'n') { out.type_ = Json::Type::Null; return literal("null"); }
        return number(out);
    }
    bool number(Json& out) {
        const char* begin = s_.c_str() + pos_;
        char* end = nullptr;
        double v = std::strtod(begin, &end);
        if (end == begin) return false;
        pos_ += static_cast<size_t>(end - begin);
        out.type_ = Json::Type::Number;
        out.num_ = v;
        return true;
    }
    static void put_utf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
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
    bool hex4(uint32_t& v) {
        if (pos_ + 4 > s_.size()) return false;
        v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }
    bool string(std::string& out) {
        ++pos_;  // opening quote
        out.clear();
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }
            if (pos_ >= s_.size()) return false;
            char e = s_[pos_++];
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
                    uint32_t cp;
                    if (!hex4(cp)) return false;
                    if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(pos_, 2, "\\u") == 0) {
                        pos_ += 2;
                        uint32_t lo;
                        if (!hex4(lo)) return false;
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    put_utf8(out, cp);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool array(Json& out) {
        ++pos_;
        out.type_ = Json::Type::Array;
        out.arr_ = std::make_shared<std::vector<Json>>();
        skip();
        if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
        while (true) {
            skip();
            Json item;
            if (!value(item)) return false;
            out.arr_->push_back(std::move(item));
            skip();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == ']') { ++pos_; return true; }
            return false;
        }
    }
    bool object(Json& out) {
        ++pos_;
        out.type_ = Json::Type::Object;
        out.obj_ = std::make_shared<std::vector<std::pair<std::string, Json>>>();
        skip();
        if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            skip();
            if (pos_ >= s_.size() || s_[pos_] != '"') return false;
            std::string key;
            if (!string(key)) return false;
            skip();
            if (pos_ >= s_.size() || s_[pos_] != ':') return false;
            ++pos_;
            skip();
            Json item;
            if (!value(item)) return false;
            out.obj_->emplace_back(std::move(key), std::move(item));
            skip();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == '}') { ++pos_; return true; }
            return false;
        }
    }

    const std::string& s_;
    size_t pos_ = 0;
};

Json Json::parse(const std::string& text, bool* ok) {
    Json out;
    bool good = JsonParser(text).run(out);
    if (ok) *ok = good;
    return good ? out : Json();
}

Int Json::size() const {
    if (type_ == Type::Array) return static_cast<Int>(arr_->size());
    if (type_ == Type::Object) return static_cast<Int>(obj_->size());
    return 0;
}

const Json& Json::operator[](Int i) const {
    if (type_ != Type::Array) fail("Json: not an array");
    return (*arr_)[index(i, arr_->size())];
}

const std::vector<Json>& Json::items() const {
    static const std::vector<Json> empty;
    return type_ == Type::Array ? *arr_ : empty;
}

bool Json::has(const std::string& key) const {
    if (type_ != Type::Object) return false;
    for (auto& kv : *obj_) if (kv.first == key) return true;
    return false;
}

const Json& Json::get(const std::string& key) const {
    if (type_ == Type::Object)
        for (auto& kv : *obj_) if (kv.first == key) return kv.second;
    return null_json();
}

const Json& Json::operator[](const std::string& key) const {
    if (!has(key)) fail("Json: missing key " + key);
    return get(key);
}

const std::vector<std::pair<std::string, Json>>& Json::members() const {
    static const std::vector<std::pair<std::string, Json>> empty;
    return type_ == Type::Object ? *obj_ : empty;
}

}  // namespace gd
