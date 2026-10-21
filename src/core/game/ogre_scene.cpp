// remake/core/game/ogre_scene.gd
// The Ogre scene (arcade.ovl FUN_800B4168 set-up, FUN_800B4528 per frame; modes.md#match-flow),
// written from the verified port `tools/research/ogre_scene_sim.py`: the defeated human's fighter is
// reloaded as the stage-9 boss, both fighters stand at the origin, and a script in the Enbu
// event format plays: music track 5, the two scripted moves (Ogre lifts the boss), a fade to
// white from frame 192, the end at 256; then a white screen for two more frames. The camera
// plays the reel of Ogre's camera id 0x4B (CameraDirector.ogre_start), then CameraDirector
// takes over.
#include "core/game/ogre_scene.h"

#include "content/fight_tables.h"
#include "content/flow_data.h"
#include "content/move_row.h"
#include "core/camera/camera_director.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fighter/character.h"
#include "core/fighter/fighter_animation.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/move_events.h"
#include "core/game/game_flow.h"
#include "core/game/mode_region.h"
#include "core/math/fx.h"
#include "core/sim_events.h"

namespace t3 {

OgreScene::OgreScene(GameFlow* flow) : FlowPart(flow) {
    moves = gd::Arr<gd::Ref<ScriptedMove>>{gd::make<ScriptedMove>(), gd::make<ScriptedMove>()};
}

// FUN_800B4168: the scene's set-up.
void OgreScene::setup() {
    gd::Ref<FightState> fight = g->fight;
    gd::Ref<ModeRegion> region = g->region;
    state = START;
    fade_on = false;
    black = false;
    letterbox = false;
    region->put8(0x69, 1);
    g->sim->events->add(SimEvents::Kind::VOICES_OFF, -1, 0);
    // FUN_8003C4AC for each, then FUN_8002BFCC facing each other (their third argument is a leftover
    // register; the scene places both at the origin below either way).
    for (const gd::Ref<FighterState>& f : fight->active()) {
        f->blend_mode = FighterAnimation::Blend::NONE;
        f->blend_mode_b = 0;
    }
    fight->blend_hold = 0x14;
    gd::Ref<FighterState> f0 = fight->fighters[0];
    gd::Ref<FighterState> f1 = fight->fighters[1];
    g->sim->place(f0, f1);
    g->sim->place(f1, f0);
    for (const gd::Ref<ScriptedMove>& m : moves) {
        m->active = 0;
        m->slot = 0;
        m->frame = 0;
    }
    fight->undrawn_mask = 3;
    gd::Int human = (region->human_mask & 1) != 0 ? 0 : 1;
    events = g->data->ogre_scripts[human];
    cursor = 0;
    gd::Ref<FighterState> f = fight->fighters[human];
    gd::Arr<gd::Int> slots = f->char_id == Character::HEIHACHI ? JIN_SLOTS : HEIHACHI_SLOTS;
    f->set_costume(slots[region->ctx8(LADDER_BOSS_COSTUME) != 0 ? 1 : 0], fight->tables->fighter);
    f->bank_type = f->char_id;
    g->sim->reload_fighter(human);
    g->sim->setup_parts(fight->fighters[1 - human]);   // the other's parts bound again (FighterSetupParts)
    for (const gd::Ref<FighterState>& other : fight->active()) {
        other->facing = FACING;
        other->tilt_x = 0;
        other->pos_x = 0;
        other->pos_y = 0;
        other->tilt_z = 0;
        other->pos_z = 0;
        other->heading = other->facing;
    }
}

// FUN_800B4528: one frame; true when the scene is over.
bool OgreScene::step() {
    fade_drawn = -1;
    g->fight->undrawn_mask = 3;
    switch (state) {
        case START:
            g->sim->camera->ogre_start(g->fight->fighters[0], g->fight->fighters[1]);
            reel_over = false;
            frame = 0;
            state = SCRIPT;
            return false;
        case CLOSING:
            fade_drawn = 0x200;
            _camera();
            frame -= 1;
            return frame < 0;
    }
    if (frame >= LETTERBOX_FROM) {
        letterbox = true;
    }
    if (frame >= events[cursor][0]) {
        while (true) {
            gd::PI32 e = events[cursor];
            if (_event(e)) {
                break;
            }
            cursor += 1;
            if (frame < events[cursor][0]) {
                break;
            }
        }
    }
    _step_moves();
    _camera();
    g->sim->view_frame();
    if (!black) {
        g->sim->background();            // FUN_8006DAB4 until the fade passes 0xD0
    }
    if (fade_on) {
        if (fade_level >= BLACK_LEVEL) {
            black = true;
        }
        fade_level = gd::mini(fade_level, FADE_MAX);
        fade_drawn = fade_level + FADE_BASE;
        fade_level += fade_speed;
    }
    frame += 1;
    return false;
}

// The scene is left: the fight's fighters are drawn again.
void OgreScene::end() {
    g->fight->undrawn_mask = 0;
}

// One event; true at the end of the script.
bool OgreScene::_event(const gd::PI32& e) {
    switch (gd::Int(e[1])) {
        case EVENT_END:
            frame = CLOSING_FRAMES;
            state = CLOSING;
            return true;
        case EVENT_MOVE_0:
        case EVENT_MOVE_1: {
            gd::Int i = e[1] - EVENT_MOVE_0;
            gd::Ref<ScriptedMove> m = moves[i];
            gd::Ref<FighterState> f = g->fight->fighters[i];
            if (m->slot != e[2]) {
                gd::Ref<MoveRow> row = g->fight->move_for_slot(f, e[2]);
                f->pose_move = row;
                f->root_move = row;
            }
            m->slot = e[2];
            m->frame = e[3];
            m->active = 1;
            m->end = e[4];
            break;
        }
        case EVENT_FADE:
            fade_level = 0;
            fade_on = true;
            fade_speed = e[2];
            break;
        case EVENT_MUSIC:
            g->music(MUSIC_OGRE);
            break;
    }
    return false;
}

// The reel until it ends, then the director (with g_cameraPhase as the fight left it).
void OgreScene::_camera() {
    gd::Ref<CameraDirector> camera = g->sim->camera;
    if (!reel_over) {
        reel_over = camera->reel_step();
    } else {
        camera->step(g->fight->fighters[0], g->fight->fighters[1]);
    }
}

// Each scripted move one pose frame on (its frame events without sounds), then animated.
void OgreScene::_step_moves() {
    gd::Ref<FightState> fight = g->fight;
    for (gd::Int i = 0; i < 2; ++i) {
        gd::Ref<ScriptedMove> m = moves[i];
        if (m->active != 1) {
            continue;
        }
        gd::Ref<FighterState> f = fight->fighters[i];
        fight->undrawn_mask &= ~gd::shl(1, i);
        f->pose_frame = (m->frame + 1) & 0xFFFF;
        f->root_frame = f->pose_frame;
        gd::Int first = g->sim->events->items.size();
        MoveEvents::run(f, fight->tables->fighter, g->sim->events);
        g->sim->move_event_calls(first);
        fight->blend_skip = 1;
        g->sim->animation->animate(f, g->sim->fighter_view, fight->blend_enabled != 0, true);
        m->frame = (m->frame + 1) & 0xFFFF;
        if (Fx::s16(m->end) < Fx::s16(m->frame)) {
            m->frame = m->end;
        }
    }
}

}  // namespace t3
