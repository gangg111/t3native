#pragma once
// remake/core/fight/round_hud.gd
// HudRoundUpdate (0x8003D904), the round part: the announcer's voices (SimEvents, run by the
// simulation) and the round messages (read by the presentation each frame). The bars, timer and
// round marks the same routine draws are plain FightState fields. See hud.md.
#include <string>

#include "gd/gd.h"

namespace t3 {

class FightState;
class SimEvents;

class RoundHud {
public:
    static constexpr gd::Int TIGER_KEY = 0x22;
    static constexpr gd::Int VOICE_WIN = 0xC640;
    static constexpr gd::Int VOICE_WIN_BOSS = 0xC64B;       // voice set 0x15

    static constexpr gd::Int TIME_UP = 1;
    static constexpr gd::Int DOUBLE_KO = 4;
    static constexpr gd::Int PERFECT = 8;
    static constexpr gd::Int GREAT = 0x10;

    static constexpr gd::Int FIRST_ROUND_INTRO = 0x3D;     // round frame of the intro voice in round 1 (1 later)
    static constexpr gd::Int FIRST_ROUND_NUMBER = 100;     // round frame of "ROUND n" in round 1 (0x28 later)
    static constexpr gd::Int LATER_ROUND_NUMBER = 0x28;
    static constexpr gd::Int FIRST_ROUND_FIGHT = 0x96;     // round frame of "FIGHT" in round 1 (0x5A later)
    static constexpr gd::Int LATER_ROUND_FIGHT = 0x5A;
    static constexpr gd::Int FIRST_ROUND_TITLE = 0x3D;     // "ROUND n" is shown from this round frame (1 later)
    static constexpr gd::Int FIRST_ROUND_READY = 0x6A;     // "READY?" is shown from this round frame (0x2E later)
    static constexpr gd::Int LATER_ROUND_READY = 0x2E;

    static constexpr gd::Int VOICE_INTRO = 0x103B;         // before the round number (0x103A in the last round)
    static constexpr gd::Int VOICE_INTRO_LAST = 0x103A;
    static constexpr gd::Int VOICE_FIGHT = 0x103C;
    static constexpr gd::Int VOICE_WINS = 0x1011;
    static constexpr gd::Int VOICE_LOSES = 0x1012;
    static constexpr gd::Int VOICE_DRAW = 0x1014;
    static constexpr gd::Int VOICE_SET_15 = 0x15;           // the voice set of the bosses with their own win voice
    static constexpr gd::Int MUSIC_FADE_FRAMES = 0xB4;
    static constexpr gd::Int BAR_LENGTH = 0x98;            // health bar pixels
    static constexpr gd::Int BAR_GROWTH = 3;               // pixels per frame a bar refills

    // One line of text: the game's FUN_8004D15C(text, palette, font, x, y) in 368 × 480 screen units.
    class Message {
    public:
        std::string text;
        gd::Int palette = 0;
        gd::Int font = 0;
        gd::Int x = 0;
        gd::Int y = 0;

        Message(const std::string& message_text, gd::Int message_palette, gd::Int message_font, gd::Int at_x,
                gd::Int at_y);
    };

    // The voices of the frame. FightFrame calls it after the round flow, before the camera, unless
    // a replay plays (also on paused frames).
    static void update(const gd::Ref<FightState>& fight, const gd::Ref<SimEvents>& events);

    // FUN_8004E520: the bars start empty at a round start and refill.
    static void reset_bars(const gd::Ref<FightState>& fight);

    // FUN_8004E55C: the health bars' lengths. A drop shows at once, a refill grows 3 pixels per
    // frame, and the recent-damage part closes 1/16 of its length (rounded up) per frame.
    static void _bars(const gd::Ref<FightState>& fight);

    // The messages shown this frame.
    static gd::Arr<gd::Ref<Message>> messages(const gd::Ref<FightState>& fight);

    // ---- voices

    // FUN_8003DCE0: the intro voice, "ROUND n" and "FIGHT".
    static void _round_start_voices(const gd::Ref<FightState>& fight, const gd::Ref<SimEvents>& events);

    // ResultWinVoice (0x80075A90): the fighter's victory voice (none for Tiger); true when played.
    static bool win_voice(const gd::Ref<FightState>& fight, gd::Int index, const gd::Ref<SimEvents>& events);

    // FUN_8003E158: the result's sound on the first frame of round state 2.
    static void _result_voice(const gd::Ref<FightState>& fight, const gd::Ref<SimEvents>& events);

    // FUN_8003E2F8: the winner's announcement on the first frame of the result display; the
    // music fades out when the match is decided.
    static void _winner_voice(const gd::Ref<FightState>& fight, const gd::Ref<SimEvents>& events);

    // FUN_80040E98: an announcer sound (SoundPlaySystem of the code's low 12 bits).
    static void _announce(gd::Int code, const gd::Ref<SimEvents>& events);

    // ---- messages

    // FUN_8003DF60: the round title and "READY?".
    static void _round_start_messages(const gd::Ref<FightState>& fight, const gd::Arr<gd::Ref<Message>>& out);

    // FUN_8003E200: the round result.
    static void _result_message(const gd::Ref<FightState>& fight, const gd::Arr<gd::Ref<Message>>& out);

    // FUN_8003E408: the winner of the round.
    static void _winner_messages(const gd::Ref<FightState>& fight, const gd::Arr<gd::Ref<Message>>& out);
};

}  // namespace t3
