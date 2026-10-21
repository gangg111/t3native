#pragma once
// remake/core/sim_events.gd
// What one simulation step asks the presentation to do (remake-plan.md#one-step): effect
// spawns, sounds, music, vibration and the like. The simulation never plays anything itself.
#include <string>

#include "gd/gd.h"

namespace t3 {

class SimEvents {
public:
    enum Kind : gd::Int {
        EFFECT,       // a: flipbook set (the owner's fighter index), b: joint
        SPARK,        // a: system flipbook index (set 3), b: joint
        DUST,         // landing dust at the fighter's root (move event 4)
        SHAKE,        // a: camera shake script (CameraShakeStart)
        VIBRATE,      // a: pad (2 both), b: pattern (PadVibrate)
        HAND,         // a: hands (bit per channel), b: shape, c: speed; applied to the fighter's hands
        LANDING_DUST, // a knock-down landing: dust ring and floor mark at `point` (FUN_8004AC28)
        SOUND,        // a: sound code, b: player (voice channel), c: attenuation (SoundPlayFighter)
        SOUND_STOP,   // a: sound code whose voice stops, b: player (SoundStopFighter)
        SYSTEM_SOUND, // a: system sound id (SoundPlaySystem)
        HIT_SPARK,    // a: 0 hit, 1 guard, b: damage of the hit; `point` the contact (HitSpawnEffect)
        MUSIC,        // a: track, −1 stops the music
        MUSIC_VOLUME, // a: volume 0-127, b: frames of the fade
        VOICE_OFF,    // a: SPU voice keyed off (SsUtKeyOffV)
        VOICES_OFF,   // the fighters' voices keyed off (FUN_8004B920); a: 1 keeps the music voices
        EFFECTS_CLEAR, // every running effect ends (round start, replay start and end)
        EFFECTS_DRAW, // a: how the flipbooks and dust rings are drawn this frame (EFFECTS_HOLD, EFFECTS_ADVANCE)
        PAUSE_MENU,   // the pause menu of player `a` (1/2) is shown this frame (FUN_80078498)
        REPLAY_TEXT,  // the blinking REPLAY caption is shown this frame
        SHAKE_REQUEST, // a: camera shake script of a move event (the fight starts it: SHAKE)
        FIGHTER_VIBRATE, // a: FighterVibrate kind of a move event (the fight turns it into VIBRATE)
        MUSIC_STOP,   // the music stops (FUN_8006C870; not a MusicPlay call)
        MUSIC_TRACK,  // a: the sound player's track, b: 1 the arcade version (Theater, FUN_8006B47C)
        MUSIC_PREPARE, // a: the track to start later (MusicPlay(track, 1))
        MOVE_LIST,    // the move lists are shown: a: the sides' mask, b: the side of the exit prompt (MoveListScreen)
        FORCE_NAMES,  // Tekken Force: fighter `a`'s name plate changes to character `b` (FUN_800B362C)
        SHOUT,        // a: the voice id (0x2nnn) of an attack shout that plays, b: player (the arcade models' face: ArcadeFace)
    };

    // `Kind.keys()`: the names of the Kind values, in order (for _to_string).
    static inline const gd::Arr<std::string> KIND_NAMES{
        "EFFECT", "SPARK", "DUST", "SHAKE", "VIBRATE", "HAND", "LANDING_DUST", "SOUND", "SOUND_STOP",
        "SYSTEM_SOUND", "HIT_SPARK", "MUSIC", "MUSIC_VOLUME", "VOICE_OFF", "VOICES_OFF", "EFFECTS_CLEAR",
        "EFFECTS_DRAW", "PAUSE_MENU", "REPLAY_TEXT", "SHAKE_REQUEST", "FIGHTER_VIBRATE", "MUSIC_STOP",
        "MUSIC_TRACK", "MUSIC_PREPARE", "MOVE_LIST", "FORCE_NAMES", "SHOUT"};

    // EFFECTS_DRAW modes: the game's flipbook and dust ring draw calls (FUN_80077744,
    // FUN_8004AEBC) with their advance flag clear (paused frames, the replay's still frames) or set
    // (FUN_8007780C and FUN_8004AF4C in the fight frame, FUN_8004A954 in the replay). A step
    // without EFFECTS_DRAW does not draw them (the loser camera, FUN_8002BDAC; between rounds).
    static constexpr gd::Int EFFECTS_HOLD = 0;
    static constexpr gd::Int EFFECTS_ADVANCE = 1;

    class Event {
    public:
        gd::Int kind = 0;
        gd::Int fighter = 0;     // the fighter whose data produced the event
        gd::Int a = 0;
        gd::Int b = 0;
        gd::Int c = 0;
        gd::PI32 point;          // world position (game units) where the event happens
        gd::PI32 vector;         // HIT_SPARK: the hit direction (the hit slot's), for the drift

        Event(gd::Int event_kind, gd::Int owner, gd::Int first, gd::Int second);

        std::string _to_string() const;
    };

    gd::Arr<gd::Ref<Event>> items;

    gd::Ref<Event> add(gd::Int kind, gd::Int fighter, gd::Int a = 0, gd::Int b = 0) const;

    gd::Ref<Event> add_at(gd::Int kind, gd::Int fighter, const gd::PI32& point, gd::Int a = 0, gd::Int b = 0) const;

    // The step's first event of this kind, or null.
    gd::Ref<Event> first(gd::Int kind) const;

    // Whether the step raised an event of this kind.
    bool has(gd::Int kind) const;

    void clear() const;
};

}  // namespace t3
