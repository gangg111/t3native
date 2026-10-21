// remake/core/camera/camera_reel.gd
// A reel of camera streams played back to back (`FUN_800673E4` / `FUN_80066D00`,
// camera.md#camera-streams-bank-section-8): the attract demonstration plays the streams of
// its bank's camera ids 0x2B, 0x2D, ...
//
// Each step turns the current stream sample into the camera source and takes the next
// sample, so the reel is one continuous sequence of samples. Reel samples are halved (toward
// zero) and authored for the projection distance 450; the eye is moved along the view line
// so that the same framing holds at the actual projection distance.
//
// The Ogre scene's reel (kind 2, one stream, then the director) is CameraDirector.ogre_start.
#include "core/camera/camera_reel.h"

#include "content/camera_tables.h"
#include "content/motion_bank.h"
#include "core/camera/camera_math.h"
#include "core/camera/camera_view.h"
#include "core/math/fx.h"

namespace t3 {

CameraReel::CameraReel(const gd::Ref<MotionBank>& motion_bank, const gd::PI32& offsets,
                       const gd::Ref<CameraTables>& camera_tables) {
    bank = motion_bank;
    streams = offsets;
    tables = camera_tables;
}

// Starts the reel from its first stream (FUN_800673E4(1)).
void CameraReel::start() {
    chooses += 1;
    stream = 0;
    next_frame = 0;
    sample = _take();
}

// True once every stream has been played; the game would then read past the reel.
bool CameraReel::finished() { return stream >= streams.size(); }

// One reel step: the camera of the current sample for projection distance `h`.
gd::Ref<CameraView> CameraReel::step(gd::Int h) {
    gd::Ref<CameraView> view = view_of(sample, h);
    view->cut = sample_starts_stream;
    sample = _take();
    return view;
}

gd::PI32 CameraReel::_take() {
    while (stream < streams.size() && next_frame >= bank->camera_frames(streams[stream])) {
        stream += 1;
        chooses += 1;
        next_frame = 0;
    }
    if (finished()) {
        sample_starts_stream = false;
        return sample;
    }
    sample_starts_stream = next_frame == 0;
    gd::PI32 s = bank->camera_sample(streams[stream], next_frame);
    next_frame += 1;
    return s;
}

// FUN_80066D00 -> FUN_800641D4 -> CameraUseSource: the view of one halved reel sample.
gd::Ref<CameraView> CameraReel::view_of(const gd::PI32& s, gd::Int h) {
    gd::PI32 rec = source_of(s, h, tables);
    return gd::make<CameraView>(CameraMath::sar6(rec[3]), CameraMath::sar6(rec[4]), rec[0], rec[1], rec[2], h);
}

// FUN_80066D00 -> FUN_800641D4: the camera source (x, y, z, pitch and yaw in 18-bit units, H) of
// one halved reel sample for projection distance `h` (a reel has no origin, yaw or mirroring).
gd::PI32 CameraReel::source_of(const gd::PI32& s, gd::Int h, const gd::Ref<CameraTables>& camera_tables) {
    gd::PI32 e{_half(s[0]), _half(s[1]), _half(s[2])};
    gd::PI32 t{_half(s[3]), _half(s[4]), _half(s[5])};
    gd::PI32 eye;
    eye.resize(3);
    for (gd::Int k = 0; k < 3; ++k)
        eye[k] = Fx::s16(t[k] + Fx::s16(Fx::div_trunc(Fx::w32((e[k] - t[k]) * h), STREAM_H)));
    gd::Int dx = t[0] - eye[0];
    gd::Int dz = t[2] - eye[2];
    gd::Int flat = CameraMath::isqrt(Fx::w32(Fx::w32(dx * dx) + Fx::w32(dz * dz)));
    gd::Int pitch = Fx::s16(CameraMath::atan2_units4096(flat, t[1] - eye[1], camera_tables)) << 6;
    gd::Int yaw = Fx::s16(CameraMath::atan2_units4096(dz, -dx, camera_tables)) << 6;
    return gd::PI32{eye[0], eye[1], eye[2], pitch, yaw, h};
}

// `(v - (v >> 31)) >> 1` on a 16-bit sample: halving toward zero.
gd::Int CameraReel::_half(gd::Int v) { return Fx::s16(Fx::div_trunc(v, 2)); }

}  // namespace t3
