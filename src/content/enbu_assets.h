#pragma once
// remake/content/enbu_assets.gd
// Everything the attract demonstrations need, loaded from the converted assets: the
// demonstrations' bank with the common bank, the tables and the camera reel once, and the
// models of each demonstration's costume slots when that demonstration is first played.
#include <string>

#include "gd/gd.h"
#include "content/enbu_data.h"     // EnbuData::Demo

namespace t3 {

class MotionSet;
class FighterTables;
class PoseTables;
class CameraTables;
class CharacterModel;
class CameraReel;
class EnbuPerformance;

class EnbuAssets {
public:
    static inline const std::string COMMON_BANK = "divmot99";

    std::string root = "";
    gd::Ref<EnbuData> data;
    gd::Ref<MotionSet> motions;
    gd::Ref<FighterTables> fighter_tables;
    gd::Ref<PoseTables> pose_tables;
    gd::Ref<CameraTables> camera_tables;
    // demonstration → (costume slot → CharacterModel)
    gd::Dict<gd::Int, gd::Dict<gd::Int, gd::Ref<CharacterModel>>> _models;
    gd::Dict<gd::Int, gd::Dict<gd::Int, gd::Ref<CharacterModel>>> _arcade_models;

    // `folder` is the asset folder (res://imported/).
    static gd::Ref<EnbuAssets> load_from(const std::string& folder);
    gd::Ref<EnbuData::Demo> demo(gd::Int number);
    // The models of demonstration `number` by costume slot (loaded once).
    gd::Dict<gd::Int, gd::Ref<CharacterModel>> models(gd::Int number);
    // Native: the models the demonstration's VIEW draws: the arcade's with `arcade` where a costume
    // has one (CharacterModel::arcade_dir), else models(number)'s; the performance keeps models().
    gd::Dict<gd::Int, gd::Ref<CharacterModel>> view_models(gd::Int number, bool arcade);
    gd::Ref<CameraReel> reel();
    // GameFlow's tests use this as GameFlow::performance_factory (bound to a Ref<EnbuAssets>).
    gd::Ref<EnbuPerformance> performance(gd::Int number);
};

}  // namespace t3
