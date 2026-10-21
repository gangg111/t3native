// remake/core/fight/replay_state.gd
// The round-end replay (`ReplayUpdate` 0x800323CC and its hooks): a ring of 300 recorded
// frames per fighter (pose and blend moves and frames, root position and rotation, anchor,
// hand, wing and power-timer values), plus the frame's sounds and the effect log, played back
// at half speed after a KO (the root is interpolated on the odd half frames).
//
// Every hook takes the live value and returns the value to use: while recording it stores the
// value, on the first played frame it also saves the live state (restored when the replay
// ends), while playing it returns the recorded one.
#include "core/fight/replay_state.h"

#include "core/ball/tekken_ball.h"
#include "core/camera/camera_director.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fighter/fighter_sounds.h"
#include "core/fighter/fighter_state.h"
#include "core/game/byte_block.h"
#include "core/game/game_mode.h"
#include "core/game/mode_region.h"
#include "core/math/fx.h"
#include "core/sim_events.h"

namespace t3 {

ReplayState::ReplayState() {
    saved = gd::Arr<gd::Ref<Frame>>{gd::make<Frame>(), gd::make<Frame>()};
    sounds.resize(FRAMES);
    log_frames.resize(FRAMES);
    for (gd::Int p = 0; p < 2; ++p) {
        for (gd::Int i = 0; i < FRAMES; ++i) {
            frames[p].append(gd::make<Frame>());
        }
    }
    for (gd::Int i = 0; i < LOG_ENTRIES; ++i) {
        log_entries.append(gd::make<LogEntry>());
    }
    for (gd::Int i = 0; i < BALL_FRAMES; ++i) {
        gd::PBytes r;
        r.resize(0x24);
        balls.append(r);
    }
}

// ReplayInit(0) at a mode start: the replay's stage word; started from the attract
// demonstration (game state 6) the replay mode is 0.
void ReplayState::init_attract(bool from_demonstration) {
    stage = 0x17;
    if (from_demonstration) {
        mode = 0;
    }
}

// ReplayInit(2) at a round start.
void ReplayState::reset() {
    mode = RECORDING;
    record = 0;
    play = 0;
    mark = -2;
    sound_delay = 0;
    bank = 0;
    skip = 0;
    recorded = 0;
}

// FUN_8004A6A4 (FUN_800777AC at a round start): the effect log starts empty.
void ReplayState::log_reset() {
    log_on = 1;
    log_frame = 0;
    log_cycle = 0;
    log_write = 0;
    log_frames[0] = 0;
}

// FUN_8004A700: logs an effect (mode 0: a flipbook of `set` and `index`; mode 1: a dust ring).
// At most 64 entries per cycle of the frame ring; the entries themselves are a ring of 64.
void ReplayState::log_effect(gd::Int set, gd::Int index, const gd::PI32& point, const gd::PI32& direction, gd::Int log_mode) {
    if (log_on == 0) {
        return;
    }
    log_cycle += 1;
    if (log_cycle >= LOG_CYCLE_LIMIT) {
        return;
    }
    log_frames[log_frame] += 1 << 16;
    gd::Ref<LogEntry> e = log_entries[log_write];
    e->mode = log_mode;
    if (log_mode == 0) {
        e->point = point.duplicate();
        e->direction = direction.duplicate();
        e->set = set;
        e->index = index;
    } else if (log_mode == 1) {
        e->point = point.duplicate();
    }
    log_write = (log_write + 1) % LOG_ENTRIES;
}

// FUN_8004A8AC: the effect log's next frame slot.
void ReplayState::log_advance() {
    if (log_on == 0) {
        return;
    }
    log_frame += 1;
    if (log_frame >= FRAMES) {
        log_frame = 0;
        log_cycle = 0;
    }
    log_frames[log_frame] = log_write;
}

// FUN_8004A954's read side: the entries of the frame slot being played, then the next slot.
gd::Arr<gd::Ref<ReplayState::LogEntry>> ReplayState::log_take() {
    gd::Arr<gd::Ref<LogEntry>> out;
    gd::Int slot = log_frames[log_read];
    gd::Int at = slot & 0xFFFF;
    for (gd::Int k = 0; k < ((slot >> 16) & 0xFFFF); ++k) {
        out.append(log_entries[(at + k) % LOG_ENTRIES]);
    }
    log_read = (log_read + 1) % FRAMES;
    return out;
}

// ReplayRecordSound (0x80033780): while recording, a sound goes to the frame slot `record +
// sound_delay` (several sounds of one frame spread over the following slots).
void ReplayState::record_sound(gd::Int kind, gd::Int player, gd::Int code) {
    if (mode != RECORDING) {
        return;
    }
    gd::Int at = record + sound_delay;
    if (at < 0) {
        at += WRAP;
    }
    sound_delay += 1;
    sounds[at % FRAMES] = (code & 0xFFFF) | (kind & 0xFF) << 16 | (player & 0xFF) << 24;
}

// FUN_80032030 at the first KO: the playback starts 30 frames before the move of the
// opponent (`opp`, the fighter that landed the KO) began.
void ReplayState::start_ko(const gd::Ref<FighterState>& opp) {
    mark = ((record - opp->pose_frame) - 0x1E) * 2;
    if (mark < 0) {
        mark += WRAP;
    }
    mark %= HALF_FRAMES;
}

// ReplayStart (0x8003209C) with `back` frames (0: from the KO mark). Without a mark (a time
// out) there is nothing to play.
void ReplayState::start(const gd::Ref<FightState>& fight, gd::Int back) {
    if (recorded < back) {
        back = recorded - 2;
    }
    if (back == 0 && mark < 0) {
        mode = DONE;
        mark = record * 2 - 0x140;
        if (mark < 0) {
            mark = record * 2 + 0x49C0;
        }
        mark %= HALF_FRAMES;
        log_on = 0;
        return;
    }
    if (back > LAST_FRAMES) {
        back = LAST_FRAMES;
    }
    if (back != 0) {
        gd::Int from = record - back;
        if (from < 0) {
            from += WRAP;
        }
        mark = (from % FRAMES) * 2;
    }
    gd::Int age = record - (mark >> 1);
    if (age < 0) {
        age += WRAP;
    }
    if (age % FRAMES < START_BACK && recorded > START_BACK + 2) {
        mark = (record - START_BACK) * 2;
    }
    if (fight->mode == GameMode::BALL) {
        mark = (record - BALL_BACK) * 2;
    }
    if (mark < 0) {
        mark += WRAP;
    }
    mode = STARTING;
    fight->blend_hold = 0x14;
    play = mark % HALF_FRAMES;
    gd::Int first = record - (play >> 1);
    if (first < 0) {
        first += WRAP;
    }
    mark = play;
    // FUN_8004A91C: the effect log is played from the same frame.
    log_read = log_frame - first % FRAMES;
    if (log_read < 0) {
        log_read += FRAMES;
    }
    log_on = 0;
}

// ReplayUpdate (0x800323CC): the recording and playback step of the frame; non-zero while the
// replay plays (the replay's first frame, STARTING, swaps the roots and sets the camera).
gd::Int ReplayState::update(const gd::Ref<FightState>& fight, const gd::Ref<FightSimulation>& sim) {
    gd::Int last = record * 2 - 2;
    gd::Int shown = fight->region->ctx_s32(TekkenBall::LAST_POINT);
    gd::Int shows = fight->region->ctx_s32(TekkenBall::RESULT);
    if (fight->mode == GameMode::BALL) {
        // Tekken Ball: the sixth replay of a lethal iron ball ends 16 frames earlier.
        if (shown == 3 && shows > 3) {
            last = record * 2 - 0x20;
        }
        if (last < 0) {
            last += HALF_FRAMES;
        }
    } else if (last < 0) {
        last = 0x256;
    }
    switch (mode) {
        case PLAYING:
            if ((fight->frame_counter & 0x10) != 0) {
                sim->events->add(SimEvents::Kind::REPLAY_TEXT, -1);
            }
            // Tekken Ball's replays run at full speed but the last.
            play += fight->mode == GameMode::BALL && shown < shows ? 2 : 1;
            if (play < 0) {
                play += WRAP;
            }
            play %= HALF_FRAMES;
            if (play == last || skip != 0) {
                mode = ENDING;
            }
            _play_sounds(fight, sim);
            return mode;
        case STARTING:
            mode = FIRST_FRAME;
            for (gd::Int p = 0; p < 2; ++p) {
                gd::Ref<FighterState> f = fight->fighters[p];
                gd::Ref<Frame> r = frames[p][(play >> 1) % FRAMES];
                saved[p]->root = gd::PI32{f->root_x, f->root_y, f->root_z};
                f->root_x = r->root[0];
                f->root_y = r->root[1];
                f->root_z = r->root[2];
            }
            sim->camera->replay_start(fight->fighters[0], fight->fighters[1]);
            for (const auto& f : fight->active()) {
                sim->hands_rest(f);
            }
            return mode;
        case FIRST_FRAME:
            mode = PLAYING;
            skip = 0;
            return mode;
        case ENDING:
            mode = DONE;
            fight->blend_hold = 0x14;
            for (const auto& f : fight->active()) {
                f->blend_frames = 0;
                f->blend_counter = 0;
                f->blend_weight = 0;
                f->blend_root_delta = gd::PI32{0, 0, 0};
            }
            return 0;
        case RECORDING: {
            if (recorded < 0x70000000) {
                recorded += 1;
            }
            record = (record + 1) % FRAMES;
            if (sound_delay > 0) {
                sound_delay -= 1;
            }
            gd::Int at = record + sound_delay;
            if (at < 0) {
                at += WRAP;
            }
            sounds[at % FRAMES] &= ~(0xFF << 16);
            return 0;
        }
        default:
            break;
    }
    return 0;
}

// FUN_800338E0: on the even half frames, the recorded sounds of the frame.
void ReplayState::_play_sounds(const gd::Ref<FightState>& fight, const gd::Ref<FightSimulation>& sim) {
    if ((play & 1) != 0) {
        return;
    }
    gd::Int s = sounds[(play >> 1) % FRAMES];
    gd::Int code = Fx::s16(s & 0xFFFF);
    gd::Int player = (s >> 24) & 0xFF;
    switch ((s >> 16) & 0xFF) {
        case 1:
            sim->events->add(SimEvents::Kind::SYSTEM_SOUND, -1, code & 0xFFF);
            break;
        case 2: {
            gd::Ref<FighterState> f = fight->fighters[player];
            FighterSounds::replay_voice(fight, player, f->voice_set, code, sim->events);
            break;
        }
        case 3:
            FighterSounds::replay_sound(fight, player, code, sim->events);
            break;
        default:
            break;
    }
}

// ==== hooks

// The replay buffer of a record: player 1's for every record but the first (the game compares
// with 0x800A96F0), so Tekken Force's third record shares the second's.
gd::Int ReplayState::_side(const gd::Ref<FighterState>& f) {
    return f->index == 0 ? 0 : 1;
}

bool ReplayState::_playing() const {
    return mode == FIRST_FRAME || mode == PLAYING || mode == 6;
}

gd::Ref<ReplayState::Frame> ReplayState::_frame_of(gd::Int p) const {
    return frames[p][(play >> 1) % FRAMES];
}

// ReplayRecordPose (0x80032DE8, after the transition blend) and ReplayPlayPose (0x80032EC8,
// at the start of FighterAnimate).
void ReplayState::record_pose(const gd::Ref<FighterState>& f) {
    if (mode != RECORDING) {
        return;
    }
    gd::Ref<Frame> r = frames[_side(f)][record];
    _store_pose(r, f);
}

void ReplayState::play_pose(const gd::Ref<FighterState>& f) {
    gd::Int p = _side(f);
    if (mode == FIRST_FRAME) {
        _store_pose(saved[p], f);
    }
    gd::Ref<Frame> r;
    if (mode == ENDING) {
        r = saved[p];
    } else if (_playing()) {
        r = _frame_of(p);
    } else {
        return;
    }
    f->root_move = r->pose_move;
    f->pose_move = r->pose_move;
    f->blend_src_move = r->blend_src;
    f->blend_dst_move = r->blend_dst;
    f->blend_src_frame = r->src_frame;
    f->blend_dst_frame = r->dst_frame;
    f->pose_frame = r->pose_frame;
    f->blend_frames = r->blend_frames;
    f->blend_mode = r->blend_bits >> 4;
    f->blend_active = r->blend_bits & 0xF;
}

void ReplayState::_store_pose(const gd::Ref<Frame>& r, const gd::Ref<FighterState>& f) {
    r->pose_move = f->pose_move;
    r->blend_src = f->blend_src_move;
    r->blend_dst = f->blend_dst_move;
    r->src_frame = f->blend_src_frame & 0xFF;
    r->dst_frame = f->blend_dst_frame & 0xFF;
    r->pose_frame = f->pose_frame & 0xFF;
    r->blend_frames = f->blend_frames & 0xFF;
    r->blend_bits = ((f->blend_mode << 4) + (f->blend_active & 0xF)) & 0xFF;
}

// ReplayRootRotation (0x80032C0C): the root rotation angles (−tilt x, heading − 0x8000, −tilt z).
gd::PI32 ReplayState::root_rotation(const gd::Ref<FighterState>& f, const gd::PI32& angles) {
    gd::Int p = _side(f);
    switch (mode) {
        case FIRST_FRAME:
        case PLAYING:
        case 6:
            if (mode == FIRST_FRAME) {
                saved[p]->angles = angles.duplicate();
            }
            return _frame_of(p)->angles.duplicate();
        case ENDING:
            return saved[p]->angles.duplicate();
        case RECORDING:
            frames[p][record]->angles = angles.duplicate();
            break;
        default:
            break;
    }
    return angles;
}

// ReplayRootPosition (0x80032838): the root (interpolated on odd half frames) and the anchor.
void ReplayState::root_position(const gd::Ref<FighterState>& f) {
    gd::Int p = _side(f);
    switch (mode) {
        case FIRST_FRAME:
        case PLAYING:
        case 6: {
            if (mode == FIRST_FRAME) {
                saved[p]->pos = gd::PI32{f->pos_x, f->pos_y, f->pos_z};
            }
            gd::Ref<Frame> a = frames[p][(play >> 1) % FRAMES];
            gd::Ref<Frame> b = frames[p][((play >> 1) + 1) % FRAMES];
            gd::PI32 root = a->root.duplicate();
            if ((play & 1) != 0) {
                for (gd::Int i = 0; i < 3; ++i) {
                    root[i] = Fx::div_trunc(a->root[i] + b->root[i], 2);
                }
            }
            f->root_x = root[0];
            f->root_y = root[1];
            f->root_z = root[2];
            f->pos_x = a->pos[0];
            f->pos_y = a->pos[1];
            f->pos_z = a->pos[2];
            break;
        }
        case ENDING: {
            gd::Ref<Frame> s = saved[p];
            f->root_x = s->root[0];
            f->root_y = s->root[1];
            f->root_z = s->root[2];
            f->pos_x = s->pos[0];
            f->pos_y = s->pos[1];
            f->pos_z = s->pos[2];
            break;
        }
        case RECORDING: {
            gd::Ref<Frame> r = frames[p][record];
            r->root = gd::PI32{f->root_x, f->root_y, f->root_z};
            r->pos = gd::PI32{f->pos_x, f->pos_y, f->pos_z};
            break;
        }
        default:
            break;
    }
}

// ReplayHandPose (0x800332D8), FUN_80033158 (the wings) and ReplayPowerTimer (0x80033600):
// one byte each.
gd::Int ReplayState::hand_pose(const gd::Ref<FighterState>& f, gd::Int value, gd::Int channel) {
    return _byte(f, value, 0, channel);
}

gd::Int ReplayState::wings(const gd::Ref<FighterState>& f, gd::Int value) {
    return _byte(f, value, 1, 0);
}

gd::Int ReplayState::power_timer(const gd::Ref<FighterState>& f) {
    return _byte(f, f->power_timer, 2, 0);
}

gd::Int ReplayState::_byte(const gd::Ref<FighterState>& f, gd::Int value, gd::Int field, gd::Int index) {
    gd::Int p = _side(f);
    switch (mode) {
        case FIRST_FRAME:
        case PLAYING:
        case 6:
            if (mode == FIRST_FRAME) {
                _set_byte(saved[p], field, index, value);
            }
            return _get_byte(_frame_of(p), field, index);
        case ENDING:
            return _get_byte(saved[p], field, index);
        case RECORDING: {
            gd::Ref<Frame> r = frames[p][record];
            _set_byte(r, field, index, value);
            break;
        }
        default:
            break;
    }
    return value;
}

void ReplayState::_set_byte(const gd::Ref<Frame>& r, gd::Int field, gd::Int index, gd::Int value) {
    switch (field) {
        case 0:
            r->hands[index] = value & 0xFF;
            break;
        case 1:
            r->wings = value & 0xFF;
            break;
        case 2:
            r->power = value & 0xFF;
            break;
        default:
            break;
    }
}

gd::Int ReplayState::_get_byte(const gd::Ref<Frame>& r, gd::Int field, gd::Int index) {
    switch (field) {
        case 0:
            return r->hands[index];
        case 1:
            return r->wings;
        default:
            break;
    }
    return r->power;
}

// FUN_800339F4: Tekken Ball's ball in the replay: recorded while the replay records (its
// position, angles, squash axis and amount, render kind and timer), played back while it plays;
// a charged ball played back starts its glow again.
void ReplayState::ball(const gd::Ref<TekkenBall>& tb) {
    gd::Ref<ByteBlock> b = tb->b;
    if (mode == RECORDING) {
        gd::PBytes r = balls[record % BALL_FRAMES];
        for (gd::Int k = 0; k < 4; ++k) {
            r.encode_u32(4 * k, b->u32(TekkenBall::B_POS + 4 * k));
        }
        r.encode_u32(0x10, b->u32(TekkenBall::B_ANGLES));
        for (gd::Int k = 0; k < 8; ++k) {
            r.encode_u8(0x1A + k, b->u8(TekkenBall::B_SQUASH_AXIS + k));
        }
        r.encode_u8(0x18, b->u8(TekkenBall::B_KIND));
        r.encode_u8(0x19, b->u8(TekkenBall::B_TIMER));
        r.encode_u16(0x22, b->u16(TekkenBall::B_SQUASH));
    } else if (mode == PLAYING || mode == FIRST_FRAME) {
        gd::PBytes r = balls[(play >> 1) % BALL_FRAMES];
        for (gd::Int k = 0; k < 4; ++k) {
            b->put32(TekkenBall::B_POS + 4 * k, r.decode_u32(4 * k));
        }
        b->put32(TekkenBall::B_ANGLES, r.decode_u32(0x10));
        for (gd::Int k = 0; k < 8; ++k) {
            b->put8(TekkenBall::B_SQUASH_AXIS + k, r.decode_u8(0x1A + k));
        }
        b->put8(TekkenBall::B_KIND, r.decode_u8(0x18));
        b->put8(TekkenBall::B_TIMER, r.decode_u8(0x19));
        b->put16(TekkenBall::B_SQUASH, r.decode_u16(0x22));
        switch (r.decode_u8(0x18)) {
            case 6:
                tb->glow(0, 0);
                break;
            case 7:
                tb->glow(1, 0);
                break;
            case 9:
                tb->glow(0, 1);
                break;
            default:
                break;
        }
    }
}

}  // namespace t3
