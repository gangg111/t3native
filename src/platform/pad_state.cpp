// remake/platform/pad_state.gd
#include "platform/pad_state.h"

namespace t3 {

std::string PadState::describe(gd::Int bits) {
    gd::Arr<std::string> parts;
    for (const auto& [bit, name] : NAMES) {
        if ((bits & bit) != 0) parts.append(name);
    }
    if (parts.is_empty()) return "\xE2\x80\x94";   // U+2014, the GDScript's literal for no bits
    std::string out;
    for (gd::Int i = 0; i < parts.size(); ++i) {
        if (i > 0) out += " ";
        out += parts[i];
    }
    return out;
}

}  // namespace t3
