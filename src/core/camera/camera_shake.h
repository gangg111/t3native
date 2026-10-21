#pragma once
// remake/core/camera/camera_shake.gd
// Camera shake (camera.md#camera-shake): CameraShakeStart (0x8004AF94) picks one of three byte
// scripts and pulses both pads through VibrateAll; CameraShakeStep (0x8004AFCC) adds the
// script's next byte to the camera's pitch every frame until -128.
#include "gd/gd.h"

namespace t3 {

class FightState;
class SimEvents;

class CameraShake {
public:
    static constexpr gd::Int END = -128;

    static void start(const gd::Ref<FightState>& fight, gd::Int script, const gd::Ref<SimEvents>& events);

    // The pitch offset of this frame (4096 units), 0 without a running script.
    static gd::Int step(const gd::Ref<FightState>& fight);
};

}  // namespace t3
