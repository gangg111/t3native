#pragma once
// remake/core/game/game_progress.gd
// The game's progress at 0x800982D0 (modes.md#save-block, #records): the 72-byte save block,
// the flow globals after it (transition target, attract step, menu cursor, ...), the 432-byte
// record block and the ranking's globals. The save file holds the save block and the records;
// the flow globals live for the session. Defaults come from the executable image.
#include <string>

#include "core/game/byte_block.h"

namespace t3 {

class GameProgress : public ByteBlock {
public:
    static constexpr gd::Int BASE = 0x800982D0LL;
    static constexpr gd::Int SIZE = 0x218;
    static constexpr gd::Int SAVE_BLOCK = 0x48;
    static constexpr gd::Int RECORDS = 0x5C;            // 0x8009832C
    static constexpr gd::Int RECORDS_SIZE = 0x1B0;
    static constexpr gd::Int TIME_RECORDS = RECORDS;    // 22 x (u32 frames, char name[4])
    static constexpr gd::Int SURVIVORS = RECORDS + 0xB0;   // 10 x (u16 character, u16 wins, char name[4])
    static constexpr gd::Int STATS = RECORDS + 0x100;      // 22 x (plays, wins, losses, draws) u16
    static constexpr gd::Int CHARACTERS = 22;
    // The save block's byte ranges (offset, length) the unlock schedule writes or reads (ProgressRules):
    // the unlocked characters and Start costumes with the arcade clears that open the Theater's movies
    // and the schedule's steps, the last unlocked character, the play-count steps and counter, the
    // unlock class and the two "new" counters. They lie in the block's first 0x48 bytes, which save
    // data starts with.
    static inline const gd::Arr<gd::Vector2i> UNLOCK_STATE{gd::Vector2i(0x00, 16), gd::Vector2i(0x22, 1),
                                                          gd::Vector2i(0x2A, 4), gd::Vector2i(0x2F, 1),
                                                          gd::Vector2i(0x36, 2)};

    ByteField<0x00, U32> unlocked{this};         // +0x00: bit per character
    ByteField<0x04, U32> start_costumes{this};   // +0x04: characters with a Start costume (costume 2)
    ByteField<0x08, U32> cleared{this};          // +0x08: arcade clears
    ByteField<0x0C, U32> cleared2{this};         // +0x0C: second clears (Panda, Tiger, Gun Jack)
    ByteField<0x10, U32> play_frames{this};      // +0x10: vertical blanks since power-on (0x800982E0)
    static constexpr gd::Int KEY_NO_USE = 12;    // the key configuration's NO USE action
    static constexpr gd::Int SPEAKER_MONO = 0;   // SPEAKER OUT: MONO, STEREO
    ByteField<0x14, U8> speaker{this};           // +0x14 SPEAKER OUT
    ByteField<0x15, U8> bgm{this};               // +0x15 BGM SELECT
    ByteField<0x16, U8> difficulty{this};        // +0x16 DIFFICULTY LEVEL
    ByteField<0x17, U8> fight_count{this};       // +0x17 FIGHT COUNT (rounds to win - 1)
    ByteField<0x18, U8> round_time{this};        // +0x18 ROUND TIME
    ByteField<0x19, U8> guard_damage{this};      // +0x19 GUARD DAMAGE
    ByteField<0x1A, U8> character_change{this};  // +0x1A CHARACTER CHANGE AT CONTINUE
    ByteField<0x1B, U8> options_colour{this};    // +0x1B the OPTION title's colour switch (0x800982EB)
    static constexpr gd::Int AUTO_SAVE = 0x21;
    ByteField<AUTO_SAVE, U8> auto_save{this};    // +0x21 AUTO SAVE
    ByteField<0x20, U8> quick_select{this};      // +0x20 QUICK SELECT
    ByteField<0x22, U8> new_character{this};     // +0x22: the last unlocked character (22: none)
    ByteField<0x23, U8> demo_position{this};     // +0x23: the demonstration fight's character position
    ByteField<0x24, U32> force_hi_score{this};   // +0x24 (0x800982F4)
    ByteField<0x28, U8> force_keys{this};        // +0x28
    ByteField<0x29, U8> cursor_hold{this};       // +0x29 SELECT CURSOR HOLD
    ByteField<0x2A, U8> play_steps{this};        // +0x2A: unlock steps opened by playing
    ByteField<0x2B, U8> play_steps_seen{this};   // +0x2B: the play count has reached 100 once
    ByteField<0x2C, U16> fights_since_unlock{this};   // +0x2C (u16)
    ByteField<0x2E, U8> ball_played{this};       // +0x2E: the first single-player Tekken Ball game was played
    ByteField<0x2F, U8> unlock_class{this};      // +0x2F: 1 at 15 characters, 2 with Doctor B.
    ByteField<0x30, U8> demo_number{this};       // +0x30 (0x80098300): the attract demonstration
    ByteField<0x36, U8> ball_new{this};          // +0x36: TEKKEN BALL MODE's "new" counter
    ByteField<0x37, U8> theater_new{this};       // +0x37: THEATER MODE's "new" counter
    ByteField<0x48, U8> transition_target{this}; // +0x48 (0x80098318)
    ByteField<0x49, U8> previous_state{this};    // +0x49
    ByteField<0x4A, U8> enbu_target{this};       // +0x4A
    ByteField<0x4B, U8> enbu_cached{this};       // +0x4B
    ByteField<0x4C, U8> screen_cached{this};     // +0x4C: the screen overlay in memory (0 none)
    ByteField<0x4D, U8> card_read{this};         // +0x4D: the start-up memory card read has been done
    ByteField<0x4E, U8> attract_step{this};      // +0x4E (0x8009831E)
    ByteField<0x50, U8> menu_cursor{this};       // +0x50 (0x80098320)
    ByteField<0x51, U8> menu_to_options{this};   // +0x51: the main menu opens on OPTION MODE
    ByteField<0x54, U32> record_entry{this};     // +0x54 (0x80098324): the time record just set (its address)
    ByteField<0x58, U32> survivor_entry{this};   // +0x58 (0x80098328): the survivor entry just made (its address)
    ByteField<0x20C, U8> ranking_page{this};     // +0x20C (0x800984DC)
    ByteField<0x20D, U8> ranking_stage{this};    // +0x20D: the ranking backdrop's stage cycle
    ByteField<0x20E, U8> name_entry{this};       // +0x20E: bit 0 time attack, bit 1 survival
    ByteField<0x20F, U8> entry_player{this};     // +0x20F
    ByteField<0x210, U8> entry_character{this};  // +0x210

    // The progress of a new game: the executable's image of the block.
    static gd::Ref<GameProgress> defaults(const gd::PBytes& image);

    explicit GameProgress(gd::Int size = SIZE);

    // Controller setting of a player (+0x1C, passed to FUN_8002A3C0).
    gd::Int controller(gd::Int player) const;
    // The last character chosen by a player (+0x1E, 0x58: none).
    gd::Int last_character(gd::Int player) const;
    void set_last_character(gd::Int player, gd::Int key) const;
    // A player's button layout: the action of L2, R2, L1, R1, Triangle, Circle, Cross, Square (+0x38).
    gd::Int button_action(gd::Int player, gd::Int button) const;
    // Whether a button has an action in the key configuration (buttons 0-7: L2 R2 L1 R1 Triangle
    // Circle Cross Square, the pad bits 1 << button).
    bool button_assigned(gd::Int player, gd::Int button) const;
    void set_button_action(gd::Int player, gd::Int button, gd::Int action) const;

    // ---- records
    gd::Int time_record(gd::Int character) const;
    void set_time_record(gd::Int character, gd::Int frames) const;
    std::string time_record_name(gd::Int character) const;
    gd::Vector2i survivor(gd::Int rank) const;
    std::string survivor_name(gd::Int rank) const;
    // A character's statistic: 0 plays, 1 wins, 2 losses, 3 draws.
    gd::Int stat(gd::Int character, gd::Int which) const;
    void set_stat(gd::Int character, gd::Int which, gd::Int v) const;
    // FUN_80051948: a character's usage, plays + losses + draws.
    gd::Int usage(gd::Int character) const;
    // FUN_8004CEF0 (strcpy): a name into the record block, with its terminator.
    void put_name(gd::Int at, const std::string& text) const;
    std::string _name(gd::Int at) const;
    // The part the save file keeps: the save block and the records.
    gd::PBytes save_data() const;
    // Save data `data` with the unlock state of the save data `kept` (UNLOCK_STATE).
    static gd::PBytes with_unlock_state(const gd::PBytes& data, const gd::PBytes& kept);
    void load_save_data(const gd::PBytes& data) const;
};

}  // namespace t3
