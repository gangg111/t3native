// remake/core/scripted/enbu_performance.gd
// The attract demonstration's performance (`enbu.ovl` FUN_800D3D64, one call per frame;
// modes.md#enbu-attract-demonstration).
//
// The first step sets both fighters up at the origin facing +x on their starting costumes and
// starts the camera reel; the next five steps wait for the music; then every step runs the due
// script events, steps each scripted move one pose frame (with its frame events), steps the
// camera reel and advances the fade. The step that runs the end event returns true. Each script
// frame's background (FUN_8006DAB4) turns the panorama with the camera (BackdropTurn): GameFlow
// hands the game's own turn state, round state and camera in.
//
// Game bug #49 (fighter 0's frame events fire twice while fighter 1 plays a move) is not
// reproduced: each fighter's events fire once.
#include "core/scripted/enbu_performance.h"

#include "content/character_model.h"
#include "content/fighter_tables.h"
#include "content/motion_bank.h"
#include "content/motion_set.h"
#include "content/move_row.h"
#include "content/pose_tables.h"
#include "core/camera/backdrop_turn.h"
#include "core/camera/camera_reel.h"
#include "core/camera/camera_view.h"
#include "core/fighter/bank_type.h"
#include "core/fighter/character.h"
#include "core/fighter/fighter_hands.h"
#include "core/fighter/fighter_skeleton.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/joint_frame.h"
#include "core/fighter/move_events.h"
#include "core/math/fx.h"
#include "core/scripted/scripted_fighter.h"

namespace t3 {

EnbuPerformance::EnbuPerformance(const gd::Ref<EnbuData::Demo>& demonstration, const gd::Ref<MotionSet>& motion_set,
    const gd::Ref<FighterTables>& tables, const gd::Ref<PoseTables>& poses,
    const gd::Dict<gd::Int, gd::Ref<CharacterModel>>& costume_models, const gd::Ref<CameraReel>& camera_reel) {
    // The members' object defaults, in declaration order, then _init.
    fighters = gd::Arr<gd::Ref<ScriptedFighter>>{gd::make<ScriptedFighter>(), gd::make<ScriptedFighter>()};
    events = gd::make<SimEvents>();
    backdrop = gd::make<BackdropTurn>();
    setup_view = gd::make<CameraView>(0, 0, 0, 0, 0, 500);
    demo = demonstration;
    motions = motion_set;
    fighter_tables = tables;
    pose_tables = poses;
    models = costume_models;
    reel = camera_reel;
}

// One frame; true on the frame that ran the end event. `music_ready`: FUN_8006BCC8's answer
// this frame (the music wait only counts down once the music is ready).
bool EnbuPerformance::step(bool music_ready) {
    events->clear();
    fade_drawn = -1;
    drawn = false;
    for (const gd::Ref<ScriptedFighter>& sf : fighters) {
        sf->drawn = false;
    }
    switch (phase) {
        case Phase::SETUP:
            _setup();
            break;
        case Phase::MUSIC_WAIT:
            if (music_ready) {
                wait -= 1;
            }
            if (music_ready && wait < 0) {
                camera = reel->step(projection);
                phase = Phase::SCRIPT;
            }
            break;
        case Phase::SCRIPT:
            return _script_frame();
    }
    return false;
}

void EnbuPerformance::_setup() {
    // FUN_8006CC44 (the stage set up), then FUN_8006CB40.
    backdrop->setup(setup_view);
    backdrop->reset();
    for (gd::Int i = 0; i < 2; ++i) {
        gd::Ref<ScriptedFighter> sf = fighters[i];
        gd::Ref<FighterState> f = sf->fighter;
        f->index = i;
        f->opp_index = 1 - i;
        f->cur_opp_index = 1 - i;
        f->player_index = i;
        f->active = 1;
        sf->state = ScriptedFighter::State::IDLE;
        sf->frame = 0;
        sf->slot = 0;
        sf->costume = demo->start_costumes[i];
        f->set_costume(sf->costume, fighter_tables);
        _bind_model(sf);
        f->hands->command(3, fighter_tables->hand_shape(f->costume_slot), 10, f->char_id);
        f->set_move(motions->row_for_slot(0));
        f->pose_frame = 1;
        f->root_frame = 1;
        f->event_frame = 0;
        f->set_anchor(gd::PI32{0, 0, 0});
        f->facing = FACING;
        f->heading = FACING;
    }
    reel->start();
    camera = reel->step(projection);
    camera_drawn = camera;
    wait = MUSIC_WAIT_FRAMES;
    phase = Phase::MUSIC_WAIT;
}

bool EnbuPerformance::_script_frame() {
    bool ended = false;
    while (cursor < demo->events.size() && demo->events[cursor][0] <= frame) {
        gd::PI32 e = demo->events[cursor];
        if (e[1] == EVENT_END) {
            ended = true;
            break;
        }
        _run_event(e);
        cursor += 1;
    }
    camera_drawn = camera;
    drawn = true;
    backdrop->step(camera, round_state, pose_tables, reel->tables);   // FUN_8006DAB4
    for (const gd::Ref<ScriptedFighter>& sf : fighters) {
        _step_fighter(sf);
    }
    // FUN_80077744(1), FUN_8004AEBC(1): the flipbooks and dust rings are drawn and advance.
    events->add(SimEvents::Kind::EFFECTS_DRAW, -1, SimEvents::EFFECTS_ADVANCE);
    camera = reel->step(projection);
    if (fade_on) {
        fade_level = gd::mini(fade_level, FADE_MAX);
        fade_drawn = fade_level + FADE_BASE;
        fade_level += fade_speed;
    }
    frame += 1;
    for (const gd::Ref<ScriptedFighter>& sf : fighters) {
        sf->fighter->event_frame = sf->fighter->pose_frame;
    }
    finished = ended;
    return ended;
}

void EnbuPerformance::_run_event(const gd::PI32& e) {
    switch (gd::Int(e[1])) {
        case EVENT_COSTUME_0:
        case EVENT_COSTUME_1: {
            gd::Ref<ScriptedFighter> sf = fighters[e[1] - EVENT_COSTUME_0];
            if (sf->costume != e[2]) {
                sf->fighter->set_costume(e[2], fighter_tables);
                sf->frame = ScriptedFighter::RELOAD_DELAY;
                sf->state = ScriptedFighter::State::RELOAD_WAIT;
            }
            sf->costume = e[2];
            break;
        }
        case EVENT_MOVE_0:
        case EVENT_MOVE_1: {
            gd::Ref<ScriptedFighter> sf = fighters[e[1] - EVENT_MOVE_0];
            if (sf->slot != e[2]) {
                sf->fighter->set_move(motions->row_for_slot(e[2]));
            }
            sf->slot = e[2];
            sf->frame = e[3];
            sf->end_frame = e[4];
            sf->state = ScriptedFighter::State::MOVE;
            break;
        }
        case EVENT_FADE:
            fade_level = 0;
            fade_on = true;
            fade_speed = e[2];
            break;
    }
}

void EnbuPerformance::_step_fighter(const gd::Ref<ScriptedFighter>& sf) {
    gd::Ref<FighterState> f = sf->fighter;
    switch (sf->state) {
        case ScriptedFighter::State::MOVE: {
            f->pose_frame = sf->frame + 1;
            f->root_frame = f->pose_frame;
            gd::Int first = events->items.size();
            MoveEvents::run(f, fighter_tables, events);
            gd::Int last = events->items.size();   // range(first, size): the end is read once
            for (gd::Int i = first; i < last; ++i) {
                events->items[i]->point = _event_point(sf, events->items[i]);
            }
            _animate(sf);
            sf->frame += 1;
            if (sf->end_frame < sf->frame) {
                sf->state = ScriptedFighter::State::IDLE;
            }
            break;
        }
        case ScriptedFighter::State::RELOAD_WAIT:
            sf->frame = Fx::s16(sf->frame - 1);
            if (sf->frame < 0) {
                sf->state = ScriptedFighter::State::RELOAD_START;
            }
            break;
        case ScriptedFighter::State::RELOAD_START:
            sf->state = ScriptedFighter::State::RELOAD_LOAD;
            break;
        case ScriptedFighter::State::RELOAD_LOAD:
            sf->state = ScriptedFighter::State::RELOAD_MODEL;
            break;
        case ScriptedFighter::State::RELOAD_MODEL:
            _bind_model(sf);
            sf->state = ScriptedFighter::State::RELOAD_PARTS;
            break;
        case ScriptedFighter::State::RELOAD_PARTS:
            sf->state = ScriptedFighter::State::IDLE;
            break;
    }
}

// FighterSetupModel: the fighter now uses the model of its costume slot. The joint blocks
// keep the last frame's joints until the next animated frame.
void EnbuPerformance::_bind_model(const gd::Ref<ScriptedFighter>& sf) {
    sf->model_costume = sf->fighter->costume_slot;
    gd::Ref<FighterSkeleton> old = sf->skeleton;
    sf->skeleton = gd::make<FighterSkeleton>(pose_tables, models.at(sf->model_costume), fighter_tables);
    if (old != nullptr) {
        sf->skeleton->root = old->root;
        sf->skeleton->joints = old->joints.duplicate();
    }
}

// FUN_8003AA6C → FighterAnimate: hands, pose, root and joints of the current frame.
void EnbuPerformance::_animate(const gd::Ref<ScriptedFighter>& sf) {
    gd::Ref<FighterState> f = sf->fighter;
    // FUN_800363B0: Mokujin's stick needs Yoshimitsu's moves, which a demonstration never uses.
    if (f->char_id == Character::MOKUJIN) {
        sf->stick_shown = f->bank_type == BankType::YOSHIMITSU;
    }
    f->hands->update(f->char_id, 0, fighter_tables);
    gd::PI32 pose = f->get_move()->anim_bank->pose(f->get_move()->anim, f->pose_frame - 1);
    sf->skeleton->update(pose, f->get_anchor(), f->facing, f->heading);
    sf->drawn = true;
}

// Where an event happens: the joint's translation, or the root for dust. MoveEvents runs
// before FighterAnimate, so these are the joints of the fighter's previous animated frame.
gd::PI32 EnbuPerformance::_event_point(const gd::Ref<ScriptedFighter>& sf, const gd::Ref<SimEvents::Event>& e) {
    gd::Arr<gd::Ref<JointFrame>> joints = sf->skeleton->joints;
    switch (e->kind) {
        case SimEvents::Kind::EFFECT:
        case SimEvents::Kind::SPARK:
            return e->b < joints.size() ? joints[e->b]->t : sf->skeleton->root->t;
        case SimEvents::Kind::DUST:
            return sf->skeleton->root->t;
    }
    return gd::PI32();
}

}  // namespace t3
