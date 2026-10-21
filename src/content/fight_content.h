#pragma once
// remake/content/fight_content.gd
// The converted assets a fight needs (`res://imported/`): the fight tables, the motion banks
// by bank type with their linked move rows (linked once, with the common bank) and the
// character models by costume slot, loaded when first used.
#include <string>

#include "gd/gd.h"

namespace t3 {

class FightTables;
class MotionBank;
class MoveRow;
class CharacterModel;
class StageData;
class TextLocale;
class GameRam;

class FightContent {
public:
    static inline const std::string MOTION = "motion";
    static inline const std::string COMMON_BANK = "divmot99";
    static inline const std::string STAGE_LETTERS = "abcdefgnijklmtupqrsv";   // stage number -> letter (stages.md)

    std::string root;
    gd::Ref<FightTables> tables;
    gd::Ref<MotionBank> common;
    gd::Arr<gd::Ref<MoveRow>> common_rows;
    gd::Dict<gd::Int, gd::Ref<MotionBank>> _banks;                // bank type -> MotionBank
    gd::Dict<gd::Int, gd::Arr<gd::Ref<MoveRow>>> _rows;           // bank type -> Array[MoveRow]
    gd::Dict<gd::Int, gd::Ref<CharacterModel>> _models;           // costume slot -> CharacterModel
    gd::Dict<gd::Int, gd::Ref<CharacterModel>> _arcade_models;    // costume slot -> the arcade's, or null
    gd::Dict<std::string, gd::Ref<StageData>> _stages;            // stage letter -> StageData
    gd::Dict<std::string, gd::PBytes> _move_texts;                // file path -> PackedByteArray
    gd::Dict<gd::Int, std::string> _move_paths;                   // costume slot -> _move_text_path, for _move_paths_locale / _english
    gd::Ref<TextLocale> _move_paths_locale;
    bool _move_paths_english = false;
    gd::Ref<GameRam> _screens_ram;                                // the memory images the mode RAMs share, read once
    gd::Dict<std::string, gd::Ref<GameRam>> _mode_rams;           // mode overlay name -> GameRam
    gd::Ref<TextLocale> locale;   // the texts' language for the mode overlays' strings (null: Japanese)

    static gd::Ref<FightContent> load_from(const std::string& folder);

    // The motion bank of a bank type (divmot<type>).
    gd::Ref<MotionBank> bank(gd::Int bank_type);
    // The linked move rows of a bank type.
    gd::Arr<gd::Ref<MoveRow>> rows(gd::Int bank_type);
    // The model of a costume slot.
    gd::Ref<CharacterModel> model(gd::Int costume_slot);
    // Native: the model the fighter's VIEW draws: the arcade's with `arcade` (the VERSION option)
    // where the costume has one, else model(). The simulation always takes model(): its skeleton
    // and parts drive the fight, which must not change with the look.
    gd::Ref<CharacterModel> view_model(gd::Int costume_slot, bool arcade);
    // The converted stage of a letter (STAGE_LETTERS, or the attract demonstration's), loaded once:
    // the fights, the ranking's backdrop and the demonstration share it (the views do not change it).
    gd::Ref<StageData> stage(const std::string& letter);
    // The stage of a stage number (out of range: the nearest one).
    gd::Ref<StageData> stage_number(gd::Int number);
    // The game's memory image with a mode overlay (`force`, `volley`) in its slot: the overlay's
    // tables and initial data, read by address.
    gd::Ref<GameRam> mode_ram(const std::string& overlay);
    // Tekken Force's level script of a stage (stage ARC member 3; empty for other stages).
    gd::PBytes level_script(gd::Int stage) const;
    // The move list of a costume slot (ARC member 4: a count, then name / command strings), empty
    // when the slot has none: the USA release's English one when English is on and it was converted
    // (its own encoding, move_text_english), else the Japanese one (the English one when the game
    // was converted from the USA disc, which has no other).
    gd::PBytes move_text(gd::Int costume_slot);
    // Whether move_text gives the USA encoding for this costume.
    bool move_text_english(gd::Int costume_slot);
    std::string _usa_path(gd::Int costume_slot) const;
    // The move list file of a costume slot, remembered while the locale and its English flag stay.
    std::string _move_text_path(gd::Int costume_slot);
    std::string _find_move_text(gd::Int costume_slot) const;
};

}  // namespace t3
