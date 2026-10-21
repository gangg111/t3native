#pragma once
// The value of a member read by name (GDScript `obj.get(name)`), classified like the comparisons
// check it: `value is int` (INT), `value is MoveRow` or null for a pointer field (ROW), anything else
// (NONE: arrays, bools, other objects) is skipped. Tables come from tools/gen_accessors.py.
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "content/move_row.h"
#include "gd/gd.h"

namespace t3test {

struct FieldValue {
    enum Kind { NONE, INT, ROW } kind = NONE;
    gd::Int i = 0;
    gd::Ref<t3::MoveRow> row;
};

inline FieldValue field_value(bool) { return {}; }   // GDScript bool is not `is int`
inline FieldValue field_value(const gd::Ref<t3::MoveRow>& r) { return {FieldValue::ROW, 0, r}; }
template <typename T>
FieldValue field_value(const T& v) {
    if constexpr (std::is_convertible_v<T, gd::Int> && !std::is_same_v<T, bool>) return {FieldValue::INT, static_cast<gd::Int>(v), nullptr};
    else return {};
}

template <typename C>
using FieldTable = std::vector<std::pair<std::string, std::function<FieldValue(const C&)>>>;

}  // namespace t3test
