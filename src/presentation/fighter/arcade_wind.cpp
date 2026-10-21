// remake/presentation/fighter/arcade_wind.gd
#include "presentation/fighter/arcade_wind.h"

#include "content/fighter_tables.h"
#include "content/pose_tables.h"
#include "core/fighter/pose_solver.h"
#include "core/math/fx.h"
#include "core/math/gte.h"
#include "presentation/fighter/arcade_attachments.h"

namespace t3 {

// Starts a fight on `stage`: still air unless it is the helicopter stage.
void ArcadeWind::start(gd::Int stage, RandomNumberGenerator& rng) {
    _phase = 0;
    vector = gd::PI32{0, 0, 0};
    _sign_x = 0;
    _sign_z = 0;
    if (stage != STAGE) {
        return;
    }
    gd::Int angle = rng.randi() & DIRECTIONS;
    _sign_x = gd::signi(ArcadeAttachments::solver()->tables->sin16(angle << 4));
    _sign_z = gd::signi(ArcadeAttachments::solver()->tables->cos16(angle << 4));
}

// FUN_8019c738: the next gust, (+-g, LIFT - g, +-g) normalised, g the low byte of a triangle wave
// of the phase.
void ArcadeWind::step() {
    gd::Int v = _phase + PHASE_STEP;
    if ((v & GUST_FLIP) != 0) {
        v = -(_phase + PHASE_STEP + 1);
    }
    _phase += PHASE_STEP;
    if (_sign_x == 0 && _sign_z == 0) {
        vector = gd::PI32{0, 0, 0};
        return;
    }
    gd::Int u = v & 0xFF;   // the gust's low byte (lbu)
    gd::PI32 raw{Fx::s16(_sign_x * u), Fx::s16(LIFT - u), Fx::s16(_sign_z * u)};
    vector = Gte::vector_normal(raw, ArcadeAttachments::fighter_tables()->rsqrt);
}

}  // namespace t3
