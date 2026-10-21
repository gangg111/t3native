#pragma once
// remake/content/ai_tables.gd
// The CPU opponent's tables (`imported/tables/ai.json`, tools/remake_import/ai.py; ai.md).
//
// Input scripts are lists of u16 steps (numpad direction in bits 0-3, buttons in bits 8-11):
// the first step is pressed when the script starts, the others on the following frames.
#include <string>

#include "gd/gd.h"

namespace t3 {

class AiTables {
public:
    static constexpr gd::Int GROUPS = 3;
    static constexpr gd::Int LEVELS = 10;

    // The character hooks (ai.md#character-hooks): against the opponent's bank (A) or playing the
    // CPU's own (B); IDLE hooks only return -1.
    enum Hook : gd::Int { NONE, IDLE, VS_PAUL, AS_KING, VS_YOSHIMITSU, AS_HWOARANG, VS_OGRE, VS_GON };
    // The converter's names (tools/remake_import/ai.py HOOK_NAMES).
    static inline const gd::Dict<std::string, gd::Int> HOOK_NAMES = [] {
        gd::Dict<std::string, gd::Int> d;
        d.set("", NONE); d.set("idle", IDLE); d.set("vs_paul", VS_PAUL); d.set("as_king", AS_KING);
        d.set("vs_yoshimitsu", VS_YOSHIMITSU); d.set("as_hwoarang", AS_HWOARANG); d.set("vs_ogre", VS_OGRE);
        d.set("vs_gon", VS_GON);
        return d;
    }();

    class Setup;

    // The per-bank-type record at 0x80022F80.
    class Bank {
    public:
        gd::Int counter = 0;                 // +0: the bank has counter moves (reactions 5)
        gd::PI32 parry_high;                 // +4: parry script against mids and highs (kind 7)
        gd::PI32 parry_low;                  // +8: against lows (kind 8)
        gd::Arr<gd::Ref<Setup>> setups;      // +0xC: the bank's setups
    };

    // A bank setup: a script started when its draw passes and a branch into `slot` is open.
    class Setup {
    public:
        gd::Int chance = 0;
        gd::Int slot = 0;
        gd::PI32 steps;
    };

    gd::Arr<gd::Arr<gd::PI32>> params;      // [group][level] -> 55 s16 words (AiLoadParams)
    gd::Arr<gd::PI32> band_thresholds;      // per character id: 6 s16 (0x80098640)
    gd::PI32 band_reach;                    // per distance band: the reach bits allowed
    gd::PI32 approach_reach;                // per reach band: reach bits that stop an approach
    gd::PI32 numpad_pads;                   // pad bits of numpad directions 0-9 (script steps)
    gd::PI32 direction_masks;               // the command direction bit of numpad 0-9
    gd::PI32 direction_pads;                // pad bits of numpad 0-9 (plain commands)
    gd::PI32 button_pads;                   // pad bits of command bits 0-3
    gd::Arr<gd::PI32> side_steps;           // AiSideStep's scripts per side
    gd::PI32 tap_forward;                   // the script of command 0xC001
    gd::PI32 tap_back;                      // 0xC002
    gd::PI32 back_dash;                     // FUN_8005968C (0x80022EB2)
    gd::PI32 wake_up;                       // 0x80022E96
    gd::Arr<gd::PI32> king_throws;
    gd::Arr<gd::PI32> king_vs_crouch;
    gd::Arr<gd::PI32> king_ground;
    gd::PI32 ogre_escapes;                  // pads against the fire breath (0x80023308)
    gd::PI32 actions;                       // the neutral actions (0x800230E0)
    gd::PI32 evasion_states;                // 0x800230EC
    gd::PI32 hook_banks;                    // AiHookForBank's table: bank type, hook A, hook B
    gd::PI32 hooks_a;                       // Hook
    gd::PI32 hooks_b;
    gd::Arr<gd::Ref<Bank>> banks;
    gd::PI32 force_words;                   // Tekken Force (M4)

    static gd::Ref<AiTables> load_from(const std::string& path);

    // The difficulty words of a group and level, clamped as unsigned values (AiLoadParams).
    gd::PI32 words(gd::Int group, gd::Int level) const;
    // AiHookForBank (0x80062B50): hook A (the opponent's bank) or B (the CPU's own).
    gd::Int hook(gd::Int bank_type, bool own) const;
    static gd::Int _hook_named(const std::string& hook_name);
    gd::Ref<Bank> bank(gd::Int bank_type) const;
};

}  // namespace t3
