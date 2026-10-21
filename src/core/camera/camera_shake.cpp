// remake/core/camera/camera_shake.gd
// Camera shake (camera.md#camera-shake): CameraShakeStart (0x8004AF94) picks one of three byte
// scripts and pulses both pads through VibrateAll; CameraShakeStep (0x8004AFCC) adds the
// script's next byte to the camera's pitch every frame until -128.
#include "core/camera/camera_shake.h"

#include "content/fight_tables.h"
#include "core/fight/fight_state.h"
#include "core/fight/vibration.h"
#include "core/sim_events.h"

namespace t3 {

void CameraShake::start(const gd::Ref<FightState>& fight, gd::Int script, const gd::Ref<SimEvents>& events) {
    fight->shake_script = script;
    fight->shake_pos = 0;
    events->add(SimEvents::Kind::SHAKE, -1, script);
    Vibration::all(fight, script, events);
}

// The pitch offset of this frame (4096 units), 0 without a running script.
gd::Int CameraShake::step(const gd::Ref<FightState>& fight) {
    if (fight->shake_script < 0) return 0;
    gd::Int value = fight->tables->shake_scripts[fight->shake_script][fight->shake_pos];
    fight->shake_pos += 1;
    if (value == END) {
        fight->shake_script = -1;
        value = 0;
    }
    return value;
}

}  // namespace t3
