#pragma once
// The GDScript value model the remake's core relies on, for a line-by-line C++ translation.
// Translation rules: RULES.md.
//
// Semantics taken from Godot 4.7.2 (core/variant/variant_op.h, doc/classes/*.xml):
// - `int` is int64_t; typed GDScript evaluates + - * / % << >> with the plain C++ int64
//   operators (`validated_evaluate`), so the same expressions in C++ (built with -fwrapv)
//   give the same results. Division by zero is an error in Godot; here it throws.
// - Packed arrays and Array / Dictionary are passed and assigned BY REFERENCE; `duplicate()`
//   makes an independent copy. `==` compares contents.
// - A packed array stores its element type: a store truncates (int32 / int64 / uint8), a read
//   widens to int. Negative indices count from the end.
//
// The containers here are handles: copying one shares the data, and every method that changes
// the contents is const (the handle does not change), so `const T&` parameters can be mutated
// through, exactly like a GDScript parameter of that type.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gd {

using Int = int64_t;

[[noreturn]] void fail(const std::string& message);

inline Int div(Int a, Int b) {
    if (b == 0) fail("Division by zero error");
    return a / b;
}
inline Int mod(Int a, Int b) {
    if (b == 0) fail("Modulo by zero error");
    return a % b;
}
// Shifts by a variable amount: the hardware Godot runs on (x86-64, arm64) uses the low 6 bits.
inline Int shl(Int a, Int b) { return static_cast<Int>(static_cast<uint64_t>(a) << (b & 63)); }
inline Int shr(Int a, Int b) { return a >> (b & 63); }

inline Int absi(Int v) { return v < 0 ? -v : v; }
inline Int mini(Int a, Int b) { return a < b ? a : b; }
inline Int maxi(Int a, Int b) { return a > b ? a : b; }
inline Int clampi(Int v, Int lo, Int hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline Int signi(Int v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
inline Int posmod(Int a, Int b) {
    Int m = mod(a, b);
    return (m < 0 && b > 0) || (m > 0 && b < 0) ? m + b : m;
}
inline std::string str(Int v) { return std::to_string(v); }

// Index normalisation shared by every container (negative: from the end).
inline size_t index(Int i, size_t size) {
    Int n = static_cast<Int>(size);
    if (i < 0) i += n;
    if (i < 0 || i >= n) fail("Index " + std::to_string(i) + " out of bounds (size " + std::to_string(n) + ")");
    return static_cast<size_t>(i);
}

// A 32-bit storage slot (Vector2i components, packed-array elements): assigning truncates to
// int32 like Godot, reading widens to int64 so arithmetic on it is 64-bit.
template <typename E>
class ElemRef {
public:
    explicit ElemRef(E* p) : p_(p) {}
    operator Int() const { return static_cast<Int>(*p_); }
    Int get() const { return static_cast<Int>(*p_); }
    const ElemRef& operator=(Int v) const { *p_ = static_cast<E>(v); return *this; }
    const ElemRef& operator=(const ElemRef& o) const { *p_ = static_cast<E>(o.get()); return *this; }
    const ElemRef& operator+=(Int v) const { return *this = get() + v; }
    const ElemRef& operator-=(Int v) const { return *this = get() - v; }
    const ElemRef& operator*=(Int v) const { return *this = get() * v; }
    const ElemRef& operator|=(Int v) const { return *this = get() | v; }
    const ElemRef& operator&=(Int v) const { return *this = get() & v; }
    const ElemRef& operator^=(Int v) const { return *this = get() ^ v; }
    const ElemRef& operator<<=(Int v) const { return *this = shl(get(), v); }
    const ElemRef& operator>>=(Int v) const { return *this = shr(get(), v); }

private:
    E* p_;
};

template <typename E>
class Packed {
public:
    Packed() : d_(std::make_shared<std::vector<E>>()) {}
    Packed(std::initializer_list<Int> values) : Packed() {
        d_->reserve(values.size());
        for (Int v : values) d_->push_back(static_cast<E>(v));
    }
    template <typename It>
    static Packed from(It first, It last) {
        Packed out;
        for (; first != last; ++first) out.d_->push_back(static_cast<E>(static_cast<Int>(*first)));
        return out;
    }

    Int size() const { return static_cast<Int>(d_->size()); }
    bool is_empty() const { return d_->empty(); }
    void resize(Int n) const { d_->resize(static_cast<size_t>(n)); }
    void clear() const { d_->clear(); }
    void append(Int v) const { d_->push_back(static_cast<E>(v)); }
    void push_back(Int v) const { append(v); }
    void append_array(const Packed& o) const { std::vector<E> copy = *o.d_; d_->insert(d_->end(), copy.begin(), copy.end()); }
    void fill(Int v) const { std::fill(d_->begin(), d_->end(), static_cast<E>(v)); }
    void remove_at(Int i) const { d_->erase(d_->begin() + index(i, d_->size())); }
    void insert(Int i, Int v) const { d_->insert(d_->begin() + i, static_cast<E>(v)); }
    void reverse() const { std::reverse(d_->begin(), d_->end()); }
    void sort() const { std::sort(d_->begin(), d_->end()); }

    // Reading: `gd::Int x = a[i];` (never `auto`: that would keep the slot, not the value).
    ElemRef<E> operator[](Int i) const { return ElemRef<E>(&(*d_)[index(i, d_->size())]); }
    Int get(Int i) const { return static_cast<Int>((*d_)[index(i, d_->size())]); }
    void set(Int i, Int v) const { (*d_)[index(i, d_->size())] = static_cast<E>(v); }

    bool has(Int v) const { for (E e : *d_) if (static_cast<Int>(e) == v) return true; return false; }
    Int find(Int v, Int from = 0) const {
        for (size_t k = static_cast<size_t>(from < 0 ? 0 : from); k < d_->size(); ++k)
            if (static_cast<Int>((*d_)[k]) == v) return static_cast<Int>(k);
        return -1;
    }
    Int count(Int v) const { Int n = 0; for (E e : *d_) if (static_cast<Int>(e) == v) ++n; return n; }

    Packed duplicate() const { Packed out; *out.d_ = *d_; return out; }
    // Godot slice(begin, end): end exclusive, negative values from the end, clamped.
    Packed slice(Int begin, Int end = INT64_MAX) const {
        Int n = size();
        if (begin < 0) begin += n;
        if (end < 0) end += n;
        begin = clampi(begin, 0, n);
        end = clampi(end, 0, n);
        Packed out;
        if (end > begin) out.d_->assign(d_->begin() + begin, d_->begin() + end);
        return out;
    }

    bool operator==(const Packed& o) const { return *d_ == *o.d_; }
    bool operator!=(const Packed& o) const { return !(*this == o); }
    bool same(const Packed& o) const { return d_ == o.d_; }

    E* data() const { return d_->data(); }
    std::vector<E>& vec() const { return *d_; }

    // Iteration yields widened values (GDScript `for x in packed`).
    class const_iterator {
    public:
        explicit const_iterator(const E* p) : p_(p) {}
        Int operator*() const { return static_cast<Int>(*p_); }
        const_iterator& operator++() { ++p_; return *this; }
        bool operator!=(const const_iterator& o) const { return p_ != o.p_; }
    private:
        const E* p_;
    };
    const_iterator begin() const { return const_iterator(d_->data()); }
    const_iterator end() const { return const_iterator(d_->data() + d_->size()); }

protected:
    std::shared_ptr<std::vector<E>> d_;
};

using PI32 = Packed<int32_t>;
using PI64 = Packed<int64_t>;

// PackedByteArray with the little-endian decode_* / encode_* methods.
class PBytes : public Packed<uint8_t> {
public:
    using Packed<uint8_t>::Packed;
    PBytes() = default;
    PBytes(const Packed<uint8_t>& p) : Packed<uint8_t>(p) {}

    Int decode_u8(Int at) const { return byte(at, 1)[0]; }
    Int decode_s8(Int at) const { return static_cast<int8_t>(byte(at, 1)[0]); }
    Int decode_u16(Int at) const { return load<uint16_t>(at); }
    Int decode_s16(Int at) const { return load<int16_t>(at); }
    Int decode_u32(Int at) const { return load<uint32_t>(at); }
    Int decode_s32(Int at) const { return load<int32_t>(at); }
    Int decode_s64(Int at) const { return load<int64_t>(at); }
    void encode_u8(Int at, Int v) const { store<uint8_t>(at, v); }
    void encode_s8(Int at, Int v) const { store<int8_t>(at, v); }
    void encode_u16(Int at, Int v) const { store<uint16_t>(at, v); }
    void encode_s16(Int at, Int v) const { store<int16_t>(at, v); }
    void encode_u32(Int at, Int v) const { store<uint32_t>(at, v); }
    void encode_s32(Int at, Int v) const { store<int32_t>(at, v); }
    PBytes duplicate() const { return PBytes(Packed<uint8_t>::duplicate()); }
    PBytes slice(Int begin, Int end = INT64_MAX) const { return PBytes(Packed<uint8_t>::slice(begin, end)); }
    std::string get_string_from_ascii() const;
    std::string get_string_from_utf8() const;

private:
    uint8_t* byte(Int at, Int n) const {
        if (at < 0 || at + n > size()) fail("decode out of bounds at " + std::to_string(at));
        return data() + at;
    }
    template <typename T>
    Int load(Int at) const { T v; std::memcpy(&v, byte(at, sizeof(T)), sizeof(T)); return static_cast<Int>(v); }
    template <typename T>
    void store(Int at, Int v) const {
        T t = static_cast<T>(v);
        std::memcpy(byte(at, sizeof(T)), &t, sizeof(T));
    }
};

// Typed Array[T]: a shared vector. Elements of RefCounted types are Ref<Class>.
template <typename T>
class Arr {
public:
    Arr() : d_(std::make_shared<std::vector<T>>()) {}
    Arr(std::initializer_list<T> values) : d_(std::make_shared<std::vector<T>>(values)) {}

    Int size() const { return static_cast<Int>(d_->size()); }
    bool is_empty() const { return d_->empty(); }
    void resize(Int n) const { d_->resize(static_cast<size_t>(n)); }
    void clear() const { d_->clear(); }
    void append(const T& v) const { d_->push_back(v); }
    void push_back(const T& v) const { d_->push_back(v); }
    void append_array(const Arr& o) const { std::vector<T> copy = *o.d_; d_->insert(d_->end(), copy.begin(), copy.end()); }
    T pop_back() const { if (d_->empty()) fail("pop_back on empty Array"); T v = d_->back(); d_->pop_back(); return v; }
    T pop_front() const { if (d_->empty()) fail("pop_front on empty Array"); T v = d_->front(); d_->erase(d_->begin()); return v; }
    void push_front(const T& v) const { d_->insert(d_->begin(), v); }
    void remove_at(Int i) const { d_->erase(d_->begin() + index(i, d_->size())); }
    void insert(Int i, const T& v) const { d_->insert(d_->begin() + i, v); }
    void fill(const T& v) const { std::fill(d_->begin(), d_->end(), v); }
    void reverse() const { std::reverse(d_->begin(), d_->end()); }
    T& back() const { if (d_->empty()) fail("back on empty Array"); return d_->back(); }
    T& front() const { if (d_->empty()) fail("front on empty Array"); return d_->front(); }

    T& operator[](Int i) const { return (*d_)[index(i, d_->size())]; }

    bool has(const T& v) const { for (const T& e : *d_) if (e == v) return true; return false; }
    Int find(const T& v) const { for (size_t k = 0; k < d_->size(); ++k) if ((*d_)[k] == v) return static_cast<Int>(k); return -1; }
    void erase(const T& v) const { Int k = find(v); if (k >= 0) remove_at(k); }

    // Shallow copy (Godot duplicate(false)); elements that are references stay shared.
    Arr duplicate() const { Arr out; *out.d_ = *d_; return out; }
    Arr slice(Int begin, Int end = INT64_MAX) const {
        Int n = size();
        if (begin < 0) begin += n;
        if (end < 0) end += n;
        begin = clampi(begin, 0, n);
        end = clampi(end, 0, n);
        Arr out;
        if (end > begin) out.d_->assign(d_->begin() + begin, d_->begin() + end);
        return out;
    }

    bool operator==(const Arr& o) const { return *d_ == *o.d_; }
    bool operator!=(const Arr& o) const { return !(*this == o); }
    bool same(const Arr& o) const { return d_ == o.d_; }

    auto begin() const { return d_->begin(); }
    auto end() const { return d_->end(); }
    std::vector<T>& vec() const { return *d_; }

private:
    std::shared_ptr<std::vector<T>> d_;
};

// Dictionary: insertion-ordered, shared. Reading a missing key is an error (as in GDScript).
template <typename K, typename V>
class Dict {
    struct Data {
        std::vector<std::pair<K, V>> items;
        std::unordered_map<K, size_t> where;
    };

public:
    Dict() : d_(std::make_shared<Data>()) {}

    Int size() const { return static_cast<Int>(d_->items.size()); }
    bool is_empty() const { return d_->items.empty(); }
    bool has(const K& k) const { return slot(k) != nullptr; }
    V& at(const K& k) const {
        std::pair<K, V>* p = slot(k);
        if (!p) fail("Dictionary: missing key");
        return p->second;
    }
    V get(const K& k, const V& fallback) const { std::pair<K, V>* p = slot(k); return p ? p->second : fallback; }
    void set(const K& k, const V& v) const {
        if (std::pair<K, V>* p = slot(k)) p->second = v;
        else insert_new(k, v);
    }
    // `d[k]` as an assignment target: inserts a default value for a new key.
    V& operator[](const K& k) const {
        if (std::pair<K, V>* p = slot(k)) return p->second;
        return insert_new(k, V()).second;
    }
    bool erase(const K& k) const {
        auto it = d_->where.find(k);
        if (it == d_->where.end()) return false;
        d_->items.erase(d_->items.begin() + it->second);
        reindex();
        return true;
    }
    void clear() const { d_->items.clear(); d_->where.clear(); }
    Arr<K> keys() const { Arr<K> out; for (auto& kv : d_->items) out.append(kv.first); return out; }
    Arr<V> values() const { Arr<V> out; for (auto& kv : d_->items) out.append(kv.second); return out; }
    Dict duplicate() const { Dict out; *out.d_ = *d_; return out; }
    bool same(const Dict& o) const { return d_ == o.d_; }
    bool operator==(const Dict& o) const { return d_->items == o.d_->items; }

    auto begin() const { return d_->items.begin(); }
    auto end() const { return d_->items.end(); }

private:
    std::pair<K, V>* slot(const K& k) const {
        auto it = d_->where.find(k);
        return it == d_->where.end() ? nullptr : &d_->items[it->second];
    }
    std::pair<K, V>& insert_new(const K& k, const V& v) const {
        d_->where.emplace(k, d_->items.size());
        d_->items.emplace_back(k, v);
        return d_->items.back();
    }
    void reindex() const {
        d_->where.clear();
        for (size_t i = 0; i < d_->items.size(); ++i) d_->where.emplace(d_->items[i].first, i);
    }
    std::shared_ptr<Data> d_;
};

// A 32-bit component (Vector2i / Vector3i): assignment truncates, reads widen to int64.
class I32 {
public:
    I32() = default;
    explicit I32(Int v) : v_(static_cast<int32_t>(v)) {}   // explicit: `cond ? i32 : int` stays int
    operator Int() const { return v_; }
    I32& operator=(Int v) { v_ = static_cast<int32_t>(v); return *this; }
    I32& operator+=(Int v) { return *this = Int(v_) + v; }
    I32& operator-=(Int v) { return *this = Int(v_) - v; }
    I32& operator*=(Int v) { return *this = Int(v_) * v; }
    bool operator==(const I32& o) const { return v_ == o.v_; }

private:
    int32_t v_ = 0;
};

// Vector2i / Vector3i: value types (copied on assignment) with 32-bit components.
struct Vector2i {
    I32 x, y;
    Vector2i() = default;
    Vector2i(Int x_, Int y_) : x(x_), y(y_) {}
    bool operator==(const Vector2i& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vector2i& o) const { return !(*this == o); }
};
struct Vector3i {
    I32 x, y, z;
    Vector3i() = default;
    Vector3i(Int x_, Int y_, Int z_) : x(x_), y(y_), z(z_) {}
    bool operator==(const Vector3i& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vector3i& o) const { return !(*this == o); }
};

// RefCounted objects: shared, nullable.
template <typename T>
using Ref = std::shared_ptr<T>;
template <typename T, typename... A>
Ref<T> make(A&&... a) { return std::make_shared<T>(std::forward<A>(a)...); }

}  // namespace gd
