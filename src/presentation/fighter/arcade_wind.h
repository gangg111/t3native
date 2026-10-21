#pragma once
// remake/presentation/fighter/arcade_wind.gd
// The arcade's wind on the helicopter stage (stage 11), which blows the arcade models' swinging
// attachments (ArcadeAttachments.wind): FUN_801e6acc picks one of eight directions when a fight
// starts, and FUN_8019c738 gusts it every step, after the fighters (they feel the last step's).
// The direction comes from the presentation's random numbers, not the fight's.
#include "gd/gd.h"
#include "presentation/fighter/blinker.h"

namespace t3 {

class ArcadeWind {
public:
    static constexpr gd::Int STAGE = 11;
    static constexpr gd::Int DIRECTIONS = 0xE00;   // rand() & 0xE00: eight directions of the 4096-unit turn
    static constexpr gd::Int PHASE_STEP = 0x20;
    static constexpr gd::Int GUST_FLIP = 0x100;    // the gust turns back when its phase reaches this bit
    static constexpr gd::Int LIFT = 0x100;

    gd::PI32 vector{0, 0, 0};                      // the wind this step (normalised to 4096, or zero)
    gd::Int _sign_x = 0;
    gd::Int _sign_z = 0;
    gd::Int _phase = 0;

    // Starts a fight on `stage`: still air unless it is the helicopter stage.
    void start(gd::Int stage, RandomNumberGenerator& rng);
    // FUN_8019c738: the next gust, (+-g, LIFT - g, +-g) normalised, g the low byte of a triangle wave
    // of the phase.
    void step();
};

}  // namespace t3
