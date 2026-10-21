#pragma once
// remake/core/fighter/fighter_input.gd
// A fighter's input state (moves.md#input-state): the pads' mapped words, InputSource and
// InputUpdate with the 60-frame history and the command buffer, the STEP key helper of
// CpuOrPadInput, InputClear and LatchButtonTaps.
#include "gd/gd.h"
#include "platform/pad_state.h"   // PadState constants

namespace t3 {

class AiDecision;
class FightState;
class FighterState;

class FighterInput {
public:
    static constexpr gd::Int HISTORY = 60;
    static constexpr gd::Int NEUTRAL = 5;
    static constexpr gd::Int STEP_UP = 0x1;                 // mapped bits of the STEP key actions (key configuration)
    static constexpr gd::Int STEP_DOWN = 0x4;
    static constexpr gd::Int STEP_HOLD = 0x23;
    static constexpr gd::Int DIRECTIONS = PadState::DIRECTIONS;
    static constexpr gd::Int OPPOSITE_VERTICAL = PadState::VERTICAL;
    static constexpr gd::Int OPPOSITE_HORIZONTAL = PadState::HORIZONTAL;
    // The attack buttons of the mapped pad word (the key configuration's layout).
    static constexpr gd::Int PAD_LP = PadState::SQUARE;
    static constexpr gd::Int PAD_RP = PadState::TRIANGLE;
    static constexpr gd::Int PAD_LK = PadState::CROSS;
    static constexpr gd::Int PAD_RK = PadState::CIRCLE;
    // A history entry: the newly pressed attack buttons in bits 4–7, the numpad direction in 0–3.
    static constexpr gd::Int HIST_LP = 0x10;
    static constexpr gd::Int HIST_RP = 0x20;
    static constexpr gd::Int HIST_LK = 0x40;
    static constexpr gd::Int HIST_RK = 0x80;
    static constexpr gd::Int HIST_BUTTONS = 0xF0;
    static constexpr gd::Int HIST_DIRECTION = 0xF;

    // The pads of one frame as FUN_8002A014 reads them: per player the physical buttons and the
    // words mapped by the key configuration, each with its newly pressed bits.
    class PadWords {
    public:
        static constexpr gd::Int REPEAT_DELAY = 0x13;          // frames a button is held before it repeats
        static constexpr gd::Int REPEAT_STEP = 4;

        gd::PI32 physical{0, 0};            // 0x800AE230
        gd::PI32 physical_pressed{0, 0};    // 0x800AE3D0
        gd::PI32 repeat{0, 0};              // 0x800AE3C8: pressed, then repeating
        gd::PI32 held{0, 0};                // 0x800A9110 (mapped)
        gd::PI32 pressed{0, 0};             // 0x800AE140 (mapped)
        gd::Int connected = 3;              // 0x800A964C: a bit per connected pad
        gd::Arr<gd::PI32> _counters{gd::PI32(), gd::PI32()};   // 0x8009C010

        PadWords();

        // FUN_8002A014 for one frame: `buttons` are the pads' physical words, `tables` per player the
        // mapped word of each physical bit. Opposite directions cancel in the mapped word.
        void read(const gd::PI32& buttons, const gd::Arr<gd::PI32>& tables);

        // The repeating buttons: a new press, then every 4th frame after the button has been held
        // for 20 frames (bits from 15 down, as the game walks them).
        gd::Int _repeat(gd::Int p);
    };

    // InputSource (0x8002BF3C): nothing (mode 0), the pad or the CPU (1), the scripted word (2,
    // InputScripted 0x8002C778: the round end's hold or crouch, the practice dummy).
    static void source(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                       const gd::Ref<PadWords>& pads, const gd::Ref<AiDecision>& cpu = nullptr);

    // CpuOrPadInput (0x8002C4CC) for a CPU fighter: AiUpdate's pad words in the fighter's own frame
    // (side 0). A CPU without health skips AiUpdate and gets no input (the game passes stack
    // garbage, bug #59).
    static void _cpu_input(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                           const gd::Ref<AiDecision>& cpu);

    // CpuOrPadInput (0x8002C4CC) for a human: the side from the screen positions, the STEP key
    // helper, then InputUpdate. Tekken Force's player faces its current opponent and reads the
    // human's pad (0x800B70D8).
    static void _pad_input(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                           const gd::Ref<PadWords>& pads);

    // FighterUpdateSide (0x800464CC): the side from the screen positions (mirrors the stick).
    static void update_side(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);

    // InputUpdate (0x8002C7D4): direction, buttons, the history rings and the command buffer. It
    // leaves the pressed bits (LP 0x10, RP 0x20, LK 0x40, RK 0x80) in $a2, which LatchButtonTaps
    // may find there (bug #52).
    static void update(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f, gd::Int side,
                       gd::Int held, gd::Int pressed);

    // InputClear (0x8002C2A4): a neutral history. It leaves 5 in $a2.
    static void clear(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f);

    // FUN_8002C33C / FUN_8002C360: opens (counters 0) or closes (−1) the command buffer.
    static void buffer_enable(const gd::Ref<FighterState>& f, bool on);

    // LatchButtonTaps (0x80040D68), once per new input frame: a new LP, RP or LP+RP press of the
    // newest history entry. While a tap is pending and a branch condition has read it
    // (condFlagUsed) the taps stay frozen, and the game then stores whatever $a2 holds as the
    // entry to compare with next time (bug #52; with the Gameplay fix, the newest entry).
    static void latch_taps(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f);
};

}  // namespace t3
