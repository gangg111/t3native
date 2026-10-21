// remake/core/ai/ai_record.gd
// A CPU fighter's AI record (0x330 bytes at 0x8009F6C0 + 0x330 · slot; ai.md#entry-and-state).
// Field comments give the record offsets; the port keeps the game's widths where values wrap.
#include "core/ai/ai_record.h"

#include "content/move_row.h"
#include "core/fighter/fighter_state.h"

namespace t3 {

AiRecord::AiRecord() {
    params.resize(PARAM_WORDS);
    for (gd::Int i = 0; i < HISTORY; ++i) {
        history.append(nullptr);
    }
}

}  // namespace t3
