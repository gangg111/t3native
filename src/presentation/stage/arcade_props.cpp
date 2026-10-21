// remake/presentation/stage/arcade_props.gd
#include "presentation/stage/arcade_props.h"

namespace t3 {

ArcadeProps::ArcadeProps(const std::string& prop_kind, const gd::PI32& sine_table) {
    kind = prop_kind;
    sine = sine_table;
    for (gd::Int p = 0; p < PROPS; ++p) {
        angles.append(gd::PI32{0, 0, 0});
        positions.append(gd::PI32{0, 0, 0});
    }
}

// The arcade's input word for a PlayStation pad's held buttons (the attack buttons only).
gd::Int ArcadeProps::arcade_buttons(gd::Int held) {
    gd::Int out = 0;
    for (const auto& kv : ARCADE_BUTTONS) {
        gd::Int button = kv.first;
        if ((held & button) != 0) {
            out |= ARCADE_BUTTONS.at(button);
        }
    }
    return out;
}

gd::Int ArcadeProps::s16(gd::Int v) {
    return ((v + 0x8000) & 0xFFFF) - 0x8000;
}

// The round's start: FUN_801E59FC or FUN_801E3DE0 (`buttons`: the first player's held buttons
// in the arcade's input word, `counter` its frame counter; their low bits pick the mode).
void ArcadeProps::start(gd::Int buttons, gd::Int counter) {
    if (kind == CAROUSEL) {
        _carousel_start();
    } else if (kind == HELICOPTER) {
        _helicopter_start(buttons, counter);
    }
}

// One frame: the carousel turns always; the helicopter only on the frames it is shown, with the
// camera's yaw (4096 units, the arcade's 0x802C6BD8).
void ArcadeProps::step(gd::Int camera_yaw) {
    if (kind == CAROUSEL) {
        _carousel_step();
    } else if (kind == HELICOPTER) {
        _helicopter_step(camera_yaw);
    }
}

// The state as verify_arcade_props.py's frame_words: angles, positions, the variables (16-bit,
// the port's field order), the manoeuvre profile.
gd::PI32 ArcadeProps::words() const {
    gd::PI32 out;
    for (const auto& a : angles) {
        out.append_array(a);
    }
    for (const auto& v : positions) {
        out.append_array(v);
    }
    for (gd::Int v : {accelerate, reverse, bob_dir, yaw_timer, yaw_dir, tilt_timer, tilt_dir, fa14,
                      phase_timer, flags, start_yaw, orbit, orbit_target, turn_left, bank, speed, step_size,
                      turn_step, turn_rate, mode, warmup, state, timer, camera_ref, camera_last, slide_x,
                      slide_z, axis, slide, bob_timer, radius, carousel_timer}) {
        out.append(s16(v));
    }
    // PackedInt32Array(profile)
    for (gd::Int v : profile) {
        out.append(v);
    }
    return out;
}

// ---- stage 3 ----

void ArcadeProps::_carousel_start() {
    for (gd::Int p = 0; p < 3; ++p) {
        positions[p] = gd::PI32{0, -300, 0xC1C};
    }
    angles[0] = gd::PI32{0, CAROUSEL_START, 0};
    angles[1] = gd::PI32{0, 0, 0};
    angles[2] = gd::PI32{0, 0x800, 0};
    angles[3] = gd::PI32{0, 0x800, 0};
    positions[3] = gd::PI32{0, 0, 0};
    carousel_timer = 0;
}

void ArcadeProps::_carousel_step() {
    angles[0][1] = s16(angles[0][1] + CAROUSEL_TURN);
    carousel_timer = s16(carousel_timer + 1);
    if (carousel_timer == CAROUSEL_FRAMES) {
        angles[0][1] = CAROUSEL_START;
        carousel_timer = 0;
    }
}

// ---- stage 11 ----

void ArcadeProps::_helicopter_start(gd::Int buttons, gd::Int counter) {
    angles[0] = gd::PI32{s16(0xFF61), 0x71C, 0};
    angles[1] = gd::PI32{0, 0, 0};
    angles[2] = gd::PI32{0, 0, 0};
    positions[0] = gd::PI32{0, s16(0xFE70), RADIUS};
    positions[1] = gd::PI32{0, s16(0xFED4), 0};
    positions[2] = gd::PI32{0, 0x118, s16(0xFB32)};
    yaw_timer = 0x80;
    fa14 = s16(0xFFFE);
    flags = 0;
    state = 0;
    bob_dir = -1;
    bob_timer = 0x20;
    yaw_dir = -1;
    tilt_timer = 0x20;
    tilt_dir = -1;
    phase_timer = 0;
    start_yaw = 0;
    orbit = 0;
    orbit_target = 0;
    camera_ref = 0;
    speed = 0;
    warmup = 0;
    slide_x = -70;
    slide_z = 70;
    timer = 0;
    axis = 1;
    bank = 1;
    radius = RADIUS;
    reverse = 1;
    if ((buttons & 0x200) != 0) {
        mode = 0;
    } else if ((buttons & 0x100) != 0) {
        mode = 1;
    } else if ((buttons & 0x40) != 0) {
        mode = 2;
    } else if ((buttons & 0x20) != 0) {
        mode = 3;
    } else {
        mode = counter & 3;
    }
}

void ArcadeProps::_helicopter_step(gd::Int camera_yaw) {
    if (warmup > WARMUP) {
        _watch_camera(s16(camera_yaw));
    } else {
        warmup = s16(warmup + 1);
    }
    _fly();
}

// FUN_801E3F68.
void ArcadeProps::_watch_camera(gd::Int yaw) {
    angles[0][1] = s16(angles[0][1] & ANGLE_MASK);
    start_yaw = s16(start_yaw & ANGLE_MASK);
    if (mode == 0) {
        if ((flags & 2) != 0) {
            camera_last = yaw;
            return;
        }
        gd::Int turned = camera_ref < yaw ? yaw - camera_ref : camera_ref - yaw;
        gd::Int bits = camera_ref < yaw ? 7 : 0x23;
        if (turned > NEAR_TURN) {
            // profile.assign(...)
            profile.vec() = (turned > FAR_TURN ? FAR_PROFILE : NEAR_PROFILE).vec();
            phase_timer = 0;
            flags = (flags | bits) & 0xFFFF;
            start_yaw = angles[0][1];
            camera_ref = yaw;
            camera_last = yaw;
            return;
        }
        timer = s16(timer + 1);
        camera_last = yaw;
    } else if (mode >= 1 && mode <= 3) {
        timer = s16(timer + 1);
        flags = (flags | 0x1000) & 0xFFFF;
    }
}

// x, z of the helicopter on the orbit at `angle` (sin and cos of -angle), rounded toward zero.
void ArcadeProps::_orbit_position(gd::Int angle, gd::Int r) {
    positions[0][0] = s16(_scaled(sine[-angle & ANGLE_MASK], r));
    positions[0][2] = s16(_scaled(sine[(-angle + QUARTER) & ANGLE_MASK], r));
}

gd::Int ArcadeProps::_scaled(gd::Int v, gd::Int r) {
    gd::Int p = v * r;
    return (p < 0 ? p + 0xFFF : p) >> 12;
}

// FUN_801E40C0.
void ArcadeProps::_fly() {
    angles[1][1] = s16(angles[1][1] - ROTOR);
    angles[2][0] = s16(angles[2][0] + ROTOR);
    if ((flags & 1) != 0 && mode == 0) {
        _manoeuvre(1);
        _manoeuvre(-1);
    } else {
        gd::Int t = yaw_timer;
        if ((t & 2) != 0) {
            angles[0][1] = s16(angles[0][1] + yaw_dir);
        }
        yaw_timer = s16(t + 1);
        if (yaw_timer == 0x100) {
            yaw_timer = 0;
            yaw_dir = s16(-yaw_dir);
        }
    }
    if ((flags & 1) == 0 || (flags & 0x1000) == 0) {
        positions[0][1] = s16(positions[0][1] + (bob_dir > 0 ? 1 : -1));
        bob_timer = s16(bob_timer + 1);
        if (bob_timer == 0x40) {
            bob_timer = 0;
            bob_dir = s16(-bob_dir);
        }
        angles[0][2] = s16(angles[0][2] + tilt_dir);
        tilt_timer = s16(tilt_timer + 1);
        if (tilt_timer == 0x40) {
            tilt_timer = 0;
            tilt_dir = s16(-tilt_dir);
        }
        if ((flags & 0x1000) == 0) {
            return;
        }
    }
    switch (mode) {
        case 1:
            _figure_1();
            break;
        case 2:
            _figure_2();
            break;
        case 3:
            _figure_3();
            break;
        default:
            break;
    }
}

// FUN_801E42DC (side 1) and its mirror FUN_801E4674 (side -1).
void ArcadeProps::_manoeuvre(gd::Int side) {
    gd::Int rise = side > 0 ? 4 : 0x20;
    gd::Int orbit_bit = side > 0 ? 8 : 0x40;
    gd::Int brake_bit = side > 0 ? 0x10 : 0x80;
    if ((flags & rise) != 0) {
        bool early = phase_timer < 0x20;
        phase_timer = s16(phase_timer + 1);
        if (early) {
            angles[0][1] = s16(angles[0][1] + 0x10 * side);
            positions[0][1] = s16(positions[0][1] + 8);
        } else {
            flags = ((flags & ~rise) | orbit_bit) & 0xFFFF;
            phase_timer = 0;
            bank = 0;
            speed = 0;
        }
    }
    if ((flags & orbit_bit) != 0) {
        gd::Int top = profile[0];
        gd::Int delta = profile[1];
        gd::Int brake = profile[2];
        gd::Int step = s16((speed >> 1) + 2);
        step_size = step;
        gd::Int gap = orbit - orbit_target;
        if ((gap < 0 && -gap < brake) || (gap >= 0 && gap < brake)) {
            step_size = top;
            if (step < top) {
                speed = s16(speed + 1);
                step_size = step;
            }
        } else {
            if ((flags & brake_bit) == 0) {
                turn_rate = profile[3];
                turn_left = (gd::absi(angles[0][1] - start_yaw) + delta) & ANGLE_MASK;
                flags = (flags | brake_bit) & 0xFFFF;
            }
            speed = s16(speed - 1);
            if (speed < 0) {
                speed = 0;
                flags = flags & ~orbit_bit & 0xFFFF;
                orbit_target = s16(orbit_target + delta * side);
            }
        }
        orbit = s16(orbit + step_size * side);
        _orbit_position(orbit);
        if (bank < 0x1E) {
            angles[0][2] = s16(angles[0][2] - 10 * side);
            bank = s16(bank + 1);
        }
    }
    if ((flags & brake_bit) != 0) {
        if (phase_timer < 0x20) {
            positions[0][1] = s16(positions[0][1] - 8);
        }
        phase_timer = s16(phase_timer + 1);
        if (phase_timer > 0x24) {
            turn_rate = s16(turn_rate - 1);
        }
        if (turn_rate < 0) {
            turn_rate = 0;
        }
        turn_step = s16(turn_rate * 2 + 2);
        angles[0][1] = s16(angles[0][1] - turn_step * side);
        gd::Int left = s16(turn_left - turn_step);
        turn_left = left & 0xFFFF;
        if (left < 0) {
            if (side > 0) {
                phase_timer = 0;
            }
            flags = flags & (side > 0 ? 0xFFEC : 0xFF7C);
        }
        if (bank > 0) {
            bank = s16(bank - 1);
            angles[0][2] = s16(angles[0][2] + 10 * side);
        }
    }
}

// FUN_801E4A0C (mode 1).
void ArcadeProps::_figure_1() {
    gd::PI32 a = angles[0];
    gd::Int t = timer;
    switch (state) {
        case 0:
        case 1:
            if (t < (state == 0 ? 200 : 0x118)) {
                a[1] = s16(a[1] + 4);
                angles[0] = a;
                return;
            }
            break;
        case 2: {
            step_size = s16((speed >> 3) | 1);
            if (t == 500) {
                accelerate = 0;
            }
            if (accelerate == 0) {
                if (t < 0x212) {
                    a[2] = s16(a[2] + bank * 6);
                }
                if (t < 0x230) {
                    a[0] = s16(a[0] + 4);
                }
                speed = s16(speed + (reverse == 0 ? 1 : -1));
                if (speed == 0) {
                    state = 5;
                    timer = 0;
                }
            } else {
                if (t < 0x1F) {
                    a[2] = s16(a[2] - bank * 6);
                }
                if (t < 0x3D) {
                    a[0] = s16(a[0] - 1);
                }
                if (gd::absi(step_size) < 8) {
                    speed = s16(speed + (reverse == 0 ? -1 : 1));
                } else {
                    step_size = reverse == 0 ? -8 : 8;
                }
            }
            gd::Int next = orbit + step_size;
            orbit = s16(next);
            a[1] = s16(a[1] - step_size);
            angles[0] = a;
            _orbit_position(next);
            return;
        }
        case 4:
            if (t < 0x118) {
                a[1] = s16(a[1] - 4);
                angles[0] = a;
                return;
            }
            break;
        case 5:
            if (t < 0x1E) {
                return;
            }
            if (t < 0xD2) {
                a[0] = s16(a[0] - 1);
            }
            if (t > 0x137) {
                if (speed == 0) {
                    reverse = 1 - reverse;
                }
                accelerate = 1;
                bank = s16(-bank);
                state = 2;
                timer = 0;
                angles[0] = a;
                return;
            }
            a[1] = s16(a[1] + (reverse != 0 ? -4 : 4));
            angles[0] = a;
            return;
        default:
            return;
    }
    state = 2;
    timer = 0;
    accelerate = 1;
}

// FUN_801E4E04 (mode 2).
void ArcadeProps::_figure_2() {
    gd::PI32 a = angles[0];
    gd::Int t = timer;
    switch (state) {
        case 0:
            if (t < 0x1E) {
                a[2] = s16(a[2] - 8);
            }
            if (t >= 0x82 && t < 0xA0) {
                a[2] = s16(a[2] + 8);
            }
            if (t < 0xA0) {
                a[1] = s16(a[1] - 0xB);
            }
            if (t == 0x8C) {
                accelerate = 1;
            }
            angles[0] = a;
            if (t < 0x8D) {
                return;
            }
            step_size = speed > 1 ? 0x19 : 5;
            if (t == 0x15E) {
                accelerate = 0;
            }
            if (accelerate == 0) {
                speed = s16(speed - 1);
                if (speed == 0) {
                    state = 2;
                    timer = 0;
                }
            } else {
                speed = s16(speed + 1);
            }
            radius = s16(radius + step_size);
            _orbit_position(orbit, radius);
            break;
        case 2:
            if (t < 0x50) {
                a[1] = s16(a[1] - 0xB);
                a[2] = s16(a[2] - 3);
            } else {
                accelerate = 1;
                state = 3;
                timer = 0;
                orbit = 0;
                speed = 0;
                a[1] = s16(a[1] - 0xB);
                a[2] = s16(a[2] - 5);
            }
            angles[0] = a;
            break;
        case 3: {
            if (t < 0x14) {
                a[1] = s16(a[1] - 0x16);
                a[2] = s16(a[2] - 5);
            } else {
                a[1] = s16(a[1] - step_size);
            }
            angles[0] = a;
            gd::Int step = (s16(speed) >> 3) | 1;
            step_size = s16(step);
            if (t == 0x578) {
                accelerate = 0;
            }
            gd::Int next_speed = speed;
            if (accelerate == 0) {
                next_speed = speed - 1;
                if (speed == 1) {
                    state = 4;
                    timer = 0;
                }
            } else {
                next_speed = speed + 1;
                if (step > 8) {
                    step_size = 8;
                    next_speed = speed;
                }
            }
            speed = s16(next_speed);
            gd::Int next = orbit + step_size;
            radius = s16(radius - 10);
            orbit = s16(next);
            if (radius < RADIUS) {
                radius = RADIUS;
            }
            _orbit_position(next, radius);
            break;
        }
        case 4:
            if (t < 100) {
                a[2] = s16(a[2] + 3);
            }
            if (t > 0xA0) {
                timer = 0;
                state = 5;
            }
            angles[0] = a;
            break;
        case 5:
            if (t < 0x15E) {
                a[1] = s16(a[1] - 5);
            } else if (t < 0x1D6) {
                a[2] = s16(a[2] + 2);
            }
            if (t < 0x3C) {
                a[0] = s16(a[0] + 1);
            }
            if (t < 0x78) {
                a[2] = s16(a[2] - 2);
            }
            if (t > 0x208) {
                state = 6;
                timer = 0;
                accelerate = 1;
            }
            angles[0] = a;
            break;
        case 6: {
            if (t < 0x1E) {
                a[0] = s16(a[0] - 1);
            }
            if (t < 0x50) {
                a[2] = s16(a[2] + 4);
            }
            gd::Int step = (s16(speed) >> 3) | 1;
            step_size = s16(step);
            gd::Int next_speed = speed;
            if (accelerate == 0) {
                next_speed = speed - 1;
                if (speed == 1) {
                    state = 7;
                    timer = 0;
                }
            } else {
                next_speed = speed + 1;
                if (step > 8) {
                    step_size = 8;
                    next_speed = speed;
                }
            }
            speed = s16(next_speed);
            orbit = s16(orbit - step_size);
            a[1] = s16(a[1] + step_size);
            angles[0] = a;
            _orbit_position(orbit);
            break;
        }
        default:
            break;
    }
}

// FUN_801E54B4 (mode 3).
void ArcadeProps::_figure_3() {
    gd::PI32 a = angles[0];
    gd::PI32 pos = positions[0];
    gd::Int t = timer;
    switch (state) {
        case 0:
            if (t < 0x1E) {
                a[0] = s16(a[0] - 1);
            }
            if (t < 0x10E) {
                pos[1] = s16(pos[1] + 10);
            } else {
                state = 1;
                timer = 0;
                slide = axis == 0 ? slide_z : slide_x;
            }
            break;
        case 1:
            if (t < 0x79) {
                gd::Int k = axis == 0 ? 0 : 2;
                pos[k] = s16(pos[k] + slide);
            } else {
                timer = 0;
                if (axis == 0) {
                    slide_z = s16(-slide_z);
                } else {
                    slide_x = s16(-slide_x);
                }
                state = 2;
                axis = 1 - axis;
            }
            break;
        case 2:
            if (t < 0x41) {
                a[1] = s16(a[1] + 0x20);
            } else {
                state = 3;
                timer = 0;
                orbit = s16(orbit + 0x800);
            }
            break;
        case 3:
            if (t >= 0xF1 && t < 0x10E) {
                a[0] = s16(a[0] + 1);
            }
            if (t < 0x10E) {
                pos[1] = s16(pos[1] - 10);
            } else {
                state = 4;
                timer = 0;
                flags &= 0xFFFD;
            }
            break;
        case 4:
            if (t >= 0x79) {
                state = 5;
                flags |= 2;
                timer = 0;
            }
            break;
        case 5:
            if (t < 0x32) {
                a[0] = s16(a[0] + 3);
                a[1] = s16(a[1] - 1);
                pos[1] = s16(pos[1] - 1);
            } else if (t < 0x118) {
                a[1] = s16(a[1] - 5);
                pos[1] = s16(pos[1] - 6);
            } else {
                state = 6;
                accelerate = 1;
                flags &= 0xFFFD;
                timer = 0;
            }
            break;
        case 6: {
            gd::Int step = (s16(speed) >> 4) * 2 + 1;
            step_size = s16(step);
            if (t == 0x66) {
                accelerate = 0;
            }
            gd::Int next_speed = speed;
            if (accelerate == 0) {
                next_speed = speed - 1;
                if (speed == 1) {
                    state = 7;
                    timer = 0;
                    flags &= 0xFFFD;
                    step_size = 5;
                }
            } else {
                next_speed = speed + 1;
                if (step > 10) {
                    step_size = 10;
                    next_speed = speed;
                }
            }
            speed = s16(next_speed);
            orbit = s16(orbit - step_size);
            a[1] = s16(a[1] + step_size);
            angles[0] = a;
            positions[0] = pos;
            _orbit_position(orbit);
            return;
        }
        case 7:
            if (t < 300) {
                a[1] = s16(a[1] + 4);
                if (t < 0x11E) {
                    pos[1] = s16(pos[1] + 5);
                }
            }
            if (t >= 0xC9 && t < 0x15E) {
                a[0] = s16(a[0] - 1);
            }
            if (t >= 0x15F) {
                state = 8;
                flags &= 0xFFFD;
                timer = 0;
            }
            break;
        case 8:
            if (t >= 0x79) {
                state = 0;
                flags |= 2;
                timer = 0;
            }
            break;
        default:
            break;
    }
    angles[0] = a;
    positions[0] = pos;
}

}  // namespace t3
