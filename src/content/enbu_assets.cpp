// remake/content/enbu_assets.gd
// Everything the attract demonstrations need, loaded from the converted assets: the
// demonstrations' bank with the common bank, the tables and the camera reel once, and the
// models of each demonstration's costume slots when that demonstration is first played.
#include "content/enbu_assets.h"

#include <string>
#include <utility>

#include "content/camera_tables.h"
#include "content/character_model.h"
#include "content/fighter_tables.h"
#include "content/motion_bank.h"
#include "content/motion_set.h"
#include "content/pose_tables.h"
#include "core/camera/camera_reel.h"
#include "core/scripted/enbu_performance.h"
#include "gd/io.h"

namespace t3 {

// `folder` is the asset folder (res://imported/).
gd::Ref<EnbuAssets> EnbuAssets::load_from(const std::string& folder) {
    gd::Ref<EnbuAssets> a = gd::make<EnbuAssets>();
    a->root = folder;
    a->data = EnbuData::load_from(gd::path_join(folder, "enbu/enbu.json"));
    gd::Ref<MotionBank> own = MotionBank::load_named(gd::path_join(folder, "motion"), a->data->bank);
    gd::Ref<MotionBank> common = MotionBank::load_named(gd::path_join(folder, "motion"), COMMON_BANK);
    a->motions = gd::make<MotionSet>(own, common);
    a->fighter_tables = FighterTables::load_from(gd::path_join(folder, "tables/fighter.json"));
    a->pose_tables = PoseTables::load_from(gd::path_join(folder, "tables/pose.json"));
    a->camera_tables = CameraTables::load_from(gd::path_join(folder, "tables/camera.json"));
    return a;
}

gd::Ref<EnbuData::Demo> EnbuAssets::demo(gd::Int number) {
    return data->demos[number];
}

// The models of demonstration `number` by costume slot (loaded once).
gd::Dict<gd::Int, gd::Ref<CharacterModel>> EnbuAssets::models(gd::Int number) {
    if (!_models.has(number)) {
        gd::Dict<gd::Int, gd::Ref<CharacterModel>> loaded;
        gd::Dict<gd::Int, std::string> names = demo(number)->models;
        for (const std::pair<gd::Int, std::string>& kv : names) {
            gd::Int slot = kv.first;
            loaded[slot] = CharacterModel::load_from(gd::path_join(root, "characters/" + names.at(slot)));
        }
        _models[number] = loaded;
    }
    return _models.at(number);
}

gd::Dict<gd::Int, gd::Ref<CharacterModel>> EnbuAssets::view_models(gd::Int number, bool arcade) {
    gd::Dict<gd::Int, gd::Ref<CharacterModel>> ps = models(number);
    if (!arcade) return ps;
    if (!_arcade_models.has(number)) {
        gd::Dict<gd::Int, gd::Ref<CharacterModel>> loaded;
        gd::Dict<gd::Int, std::string> names = demo(number)->models;
        for (const std::pair<gd::Int, std::string>& kv : names) {
            std::string dir = CharacterModel::arcade_dir(gd::path_join(root, "characters/" + kv.second));
            loaded[kv.first] = dir.empty() ? ps.at(kv.first) : CharacterModel::load_from(dir);
        }
        _arcade_models[number] = loaded;
    }
    return _arcade_models.at(number);
}

gd::Ref<CameraReel> EnbuAssets::reel() {
    return gd::make<CameraReel>(motions->own, data->camera_reel, camera_tables);
}

gd::Ref<EnbuPerformance> EnbuAssets::performance(gd::Int number) {
    // Arguments in GDScript's left-to-right order (models() loads and caches, reel() makes a reel).
    gd::Ref<EnbuData::Demo> d = demo(number);
    gd::Dict<gd::Int, gd::Ref<CharacterModel>> m = models(number);
    gd::Ref<CameraReel> r = reel();
    return gd::make<EnbuPerformance>(d, motions, fighter_tables, pose_tables, m, r);
}

}  // namespace t3
