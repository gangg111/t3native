#pragma once
// remake/core/effects/effect_pool.gd
// The state of the effect objects (EffectObjects): the pool in allocation order and the
// globals of EffectsUpdate.
#include "core/effects/effect_objects.h"
#include "gd/gd.h"

namespace t3 {

class EffectPool {
public:
    gd::Arr<gd::Ref<EffectObjects::Obj>> objects;
    gd::Int live = 0;                       // 0x800A965C: objects updated by the last EffectsUpdate
    gd::Int replay_glows = 0;               // 0x800A0D38: the glows were re-created for the replay
    gd::Int end_cleared = 0;                // 0x800A0D39: the pool was emptied for the round end
    gd::Int burn_flags = 0;                 // 0x800AE39E: Gon's burning players (0x10 >> player)
    gd::PI32 glow_mask{0, 0};               // 0x800A8B28: glowing joints per player
    gd::Int counter = 0;                    // 0x80098BDC
    gd::Int echo_voice = 0;                 // 0x800A0D40: SPU voice of the next replay echo
    // projectile segments (start, end) per owner
    // (GDScript `Array[Array]`; the inner arrays hold PackedInt32Array(6) segments.)
    gd::Arr<gd::Arr<gd::PI32>> segments{gd::Arr<gd::PI32>(), gd::Arr<gd::PI32>()};
    gd::Arr<gd::PI32> lights;               // the frame's point lights (x, y, z, kind)
};

}  // namespace t3
