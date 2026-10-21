// remake/platform/pad_vibration.gd
#include "platform/pad_vibration.h"

namespace t3 {

void PadVibration::MotorScript::start(const gd::PI32& words) {
    if (words.is_empty() || (words[0] & 0xF000) == 0) {
        return;
    }
    steps = words;
    index = 0;
    count = 0;
}

// Advances one frame; returns the running step's word, or −1.
gd::Int PadVibration::MotorScript::step() {
    if (index < 0) {
        return -1;
    }
    gd::Int word = steps[index];
    count += 1;
    if ((word & 0xFF) <= count) {
        count = 0;
        index = (word & 0xF000) == CONTINUE && index + 1 < steps.size() ? index + 1 : -1;
    }
    return word;
}

PadVibration::PadVibration(const gd::Ref<FightTables::VibrationTables>& vibration) {
    tables = vibration;
    for (gd::Int pad = 0; pad < PADS; ++pad) {
        pending.append(gd::PI32{0, 0, 0, 0});
        gd::Arr<gd::Ref<MotorScript>> s;
        gd::Arr<gd::Ref<MotorScript>> l;
        for (gd::Int slot = 0; slot < SLOTS; ++slot) {
            s.append(gd::make<MotorScript>());
            l.append(gd::make<MotorScript>());
        }
        small.append(s);
        large.append(l);
    }
}

// PadVibrate(pad, pattern): pad 2 is both.
void PadVibration::vibrate(gd::Int pad, gd::Int pattern) {
    if (pattern < 0 || pattern >= tables->slots.size()) {
        return;
    }
    gd::Int slot = tables->slots[pattern];
    if (slot >= SLOTS) {
        return;
    }
    for (gd::Int p = 0; p < PADS; ++p) {
        if (pad == p || pad >= PADS) {
            pending[p][slot] = pattern;
        }
    }
}

// Stops every script (a new fight, the pause menu leaving).
void PadVibration::clear() {
    for (gd::Int p = 0; p < PADS; ++p) {
        pending[p] = gd::PI32{0, 0, 0, 0};
        for (gd::Int slot = 0; slot < SLOTS; ++slot) {
            small[p][slot]->index = -1;
            large[p][slot]->index = -1;
        }
        small_on[p] = false;
        large_level[p] = 0;
    }
}

// One frame of the scripts driving both players' motors (Pads); `enabled` false holds them still
// (the Vibration setting off).
void PadVibration::drive(bool enabled) {
    update();
    for (gd::Int p = 0; p < PADS; ++p) {
        if (Pads_set_motors) Pads_set_motors(p, small_on[p] && enabled, enabled ? gd::Int(large_level[p]) : 0);
    }
}

// Both players' motors stopped (a fight that ends, the application quitting).
void PadVibration::stop_motors() {
    for (gd::Int p = 0; p < PADS; ++p) {
        if (Pads_set_motors) Pads_set_motors(p, false, 0);
    }
}

// One frame (VibrationUpdate for both pads).
void PadVibration::update() {
    gd::Int phase = _frame & 7;
    _frame += 1;
    for (gd::Int p = 0; p < PADS; ++p) {
        for (gd::Int slot = 0; slot < SLOTS; ++slot) {
            gd::Int pattern = pending[p][slot];
            if (pattern != 0) {
                small[p][slot]->start(tables->small[pattern]);
                large[p][slot]->start(tables->large[pattern]);
                pending[p][slot] = 0;
            }
        }
        bool on = false;
        gd::Int level = 0;
        for (gd::Int slot = 0; slot < SLOTS; ++slot) {
            gd::Int word = small[p][slot]->step();
            if (word >= 0) {
                on = (gd::Int(tables->pulses[(word & 0xF00) >> 8]) & gd::shl(1, phase)) != 0;
            }
            word = large[p][slot]->step();
            if (word >= 0) {
                level = ((word & 0xF00) >> 4) + 15;
            }
        }
        small_on[p] = on;
        large_level[p] = level;
    }
}

}  // namespace t3
