#pragma once
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
#include "gd/gd.h"

namespace t3 {

class MotionBank;
class CameraTables;
class CameraView;

class CameraReel {
public:
    static constexpr gd::Int STREAM_H = 0x1C2;            // the projection distance the reel was authored for

    gd::Ref<MotionBank> bank;
    gd::PI32 streams;                  // section-8 offsets, in order
    gd::Ref<CameraTables> tables;
    gd::Int stream = 0;                // index of the playing stream
    gd::Int next_frame = 0;            // its next sample
    gd::PI32 sample;                   // the sample the next step uses
    bool sample_starts_stream = false; // `sample` is the first of its stream (a cut)
    gd::Int chooses = 0;               // CameraChoose calls so far (each draws a random number)

    CameraReel(const gd::Ref<MotionBank>& motion_bank, const gd::PI32& offsets,
               const gd::Ref<CameraTables>& camera_tables);

    // Starts the reel from its first stream (FUN_800673E4(1)).
    void start();

    // True once every stream has been played; the game would then read past the reel.
    bool finished();

    // One reel step: the camera of the current sample for projection distance `h`.
    gd::Ref<CameraView> step(gd::Int h);

    gd::PI32 _take();

    // FUN_80066D00 -> FUN_800641D4 -> CameraUseSource: the view of one halved reel sample.
    gd::Ref<CameraView> view_of(const gd::PI32& s, gd::Int h);

    // FUN_80066D00 -> FUN_800641D4: the camera source (x, y, z, pitch and yaw in 18-bit units, H) of
    // one halved reel sample for projection distance `h` (a reel has no origin, yaw or mirroring).
    static gd::PI32 source_of(const gd::PI32& s, gd::Int h, const gd::Ref<CameraTables>& camera_tables);

    // `(v - (v >> 31)) >> 1` on a 16-bit sample: halving toward zero.
    static gd::Int _half(gd::Int v);
};

}  // namespace t3
