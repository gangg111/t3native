#pragma once
// remake/core/fighter/fighter_sounds.gd
// FighterSounds (0x80041138, sound.md#fighter-sound-logic): the fighter's voices and sounds of
// the frame, the sound scripts, and the voice cooldowns. The simulation only decides what plays
// (SimEvents SOUND / SOUND_STOP / SYSTEM_SOUND with the game's 16-bit sound codes); every sound
// is also written to the replay. `rand()` is drawn exactly where the game draws it.
#include "content/fight_tables.h"   // FightTables::VoiceSet
#include "gd/gd.h"

namespace t3 {

class FightState;
class FighterState;
class SimEvents;

class FighterSounds {
public:
    static constexpr gd::Int VOICE_FIRST = 2;               // voice ids 0x2nnn–0x5nnn: category nibble − 2
    static constexpr gd::Int VOICE_CATEGORIES = 4;
    static constexpr gd::Int SHOUT_CATEGORY = 2;            // category nibble of the attack shouts (SimEvents SHOUT)
    static constexpr gd::Int THROW_VOICE = 4;               // category nibble not written to the replay during a throw
    static constexpr gd::Int KO_VOICE = 0x4000;
    static constexpr gd::Int KO_IMPACT_LIGHT = 0x7044;
    static constexpr gd::Int KO_IMPACT_HEAVY = 0x7048;
    static constexpr gd::Int HEAVY_JOINT = 0xB;             // first descriptor joint from which impacts are heavy
    static constexpr gd::Int GUARD_JOINT = 0xC;
    static constexpr gd::Int GUARD_LIGHT = 0x704D;
    static constexpr gd::Int GUARD_HEAVY = 0x704E;
    static constexpr gd::Int WOOD_GUARD_LIGHT = 0x707B;
    static constexpr gd::Int WOOD_GUARD_HEAVY = 0x707A;
    static inline const gd::Arr<gd::Int> STRIKE_CODES{0x7000, 0x7041, 0x7042, 0x7045};
    static constexpr gd::Int JUMP = 0x1053;
    static constexpr gd::Int LAND_A = 0x1054;
    static constexpr gd::Int LAND_B = 0x104F;
    static constexpr gd::Int SWING = 0x604A;
    static constexpr gd::Int DAMAGE_VOICE_HOLD = 0x28;
    static constexpr gd::Int HEAVY_DAMAGE = 0x14;
    static constexpr gd::Int GUN_JACK_LIGHT = 0x705D;
    static constexpr gd::Int GUN_JACK_HEAVY_A = 0x7099;
    static constexpr gd::Int GUN_JACK_HEAVY_B = 0x7098;
    static constexpr gd::Int REPLAY_SYSTEM = 1;
    static constexpr gd::Int REPLAY_VOICE = 2;
    static constexpr gd::Int REPLAY_SOUND = 3;

    // FighterSounds for fighter `f` against `opp` (the fight calls it for each active fighter).
    static void run(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                    const gd::Ref<FighterState>& opp, const gd::Ref<SimEvents>& events);

    // SoundScriptStop (0x80041C30).
    static void script_stop(const gd::Ref<FighterState>& f);

    // Steps 5–8 when no sound script runs: the damage voice, the attack shout, jump and landing
    // sounds and the swing one frame before the active window.
    // `voiced` is assigned in the body, so it is taken by value.
    static void _ambient(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                         const gd::Ref<FighterState>& opp, const gd::Ref<FightTables::VoiceSet>& voices,
                         bool voiced, const gd::Ref<SimEvents>& events);

    // The running sound script: every entry of the script's frame counter.
    static void _run_script(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                            const gd::Ref<FighterState>& opp, const gd::Ref<SimEvents>& events);

    // Sound list nibbles 1 and 7 on contact: the impact sound of a hit or a guard.
    static void _impact(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                        const gd::Ref<FighterState>& opp, gd::Int code, const gd::Ref<SimEvents>& events);

    // FUN_80041E08: Gun Jack's hit sounds (the game tests charId 0x10). The game alternates the heavy ones by the VSync counter
    // (0x800AE6E0); the remake counts one VSync per frame.
    static void _gun_jack_hit(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                              const gd::Ref<FighterState>& opp, const gd::Ref<SimEvents>& events);

    // FUN_80041F4C: the strike sound by the attacker's damage and descriptor.
    static void _strike(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                        const gd::Ref<SimEvents>& events);

    // FUN_80041C48: 0 holds the jump and landing sounds this frame, 1 answers the opponent's sound
    // script with the special voice and stops it, 2 starts a one-frame voice cooldown, 3 plays one
    // of three special sounds for the opponent.
    static void _special(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f,
                         const gd::Ref<FighterState>& opp, gd::Int n, const gd::Ref<SimEvents>& events);

    // FUN_80040EB8 / FUN_80041038: a voice or sound the replay plays back (not recorded again).
    static void replay_voice(const gd::Ref<FightState>& fight, gd::Int player, gd::Int voice_set, gd::Int code,
                             const gd::Ref<SimEvents>& events);

    static void replay_sound(const gd::Ref<FightState>& fight, gd::Int player, gd::Int code,
                             const gd::Ref<SimEvents>& events);

    static bool _is_voice(gd::Int code);

    // A voice id (0x2nnn–0x5nnn) of `player` with voice set `voice_set`: written to the replay
    // unless it is a throw voice during a throw (FUN_80075BFC), then FighterVoice (0x80040F28).
    static void _voice(const gd::Ref<FightState>& fight, gd::Int player, gd::Int voice_set, gd::Int id,
                       const gd::Ref<SimEvents>& events);

    // A sound id of `player`, written to the replay (FighterSoundById 0x8004108C): a zero top
    // nibble stops the sound's voice.
    static void _sound(const gd::Ref<FightState>& fight, gd::Int player, gd::Int id,
                       const gd::Ref<SimEvents>& events);
};

}  // namespace t3
