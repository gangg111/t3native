# Stage 2: making the C++ port playable on Android

Done: `remake/core` + content loaders in C++ (`t3native/src`), bit-exact with the original game
(42 fight traces, 15 whole-game flow traces). Left: the Godot presentation layer
(`remake/presentation`, 10.8k lines in 52 files; `app/game.gd` 887 lines; 11 shaders incl. a
226-line GPU skinning include), audio, movies, input (touch + pads), settings/save, and the app.

Measured constraints:
- Presentation uses Godot's scene graph: Control/draw_* (2D canvas, ~140 calls), ArrayMesh +
  MeshInstance3D, StandardMaterial3D (17) + ShaderMaterial (16), Camera3D, Directional/Omni lights,
  Environment (glow), SubViewport, ImmediateMesh, MultiMesh, AudioStreamPlayer, VideoStreamPlayer.
  None of it translates mechanically; a small engine layer is needed under it.
- Assets (converter output, the user's own disc): PNG (~580), WAV (416 sfx), OGG Vorbis music (47,
  96 MB), OGV Theora+Vorbis movies (48, 170 MB), JSON, bin(.gz). Android MediaCodec has no Theora.
- Visuals may differ from the original ("pixels, shading, effects' look may differ"); the remake's
  look (shadows, glow, smooth textures) is the reference.
- No phone connected; an Android emulator (API 36.1 x86_64, WHPX accel, host GPU) is available.
  Android SDK build-tools 34-36 and NDK r26 are installed; no Gradle downloads wanted.

## Architecture

- `src/core` (done) untouched; presentation reads it, never writes (as in the remake).
- `src/gfx`: a thin GLES 3.0 renderer: textures, meshes (VBO/VAO), shader programs, render targets,
  a 2D batcher (rects, textured quads, polygons, glyph text) for the Control/draw_* code, and a
  minimal 3D pass (camera, skinned fighters, stage meshes, one directional light + ambient,
  shadow map, glow post-pass). Shaders: the 11 gdshaders hand-ported to GLSL ES 3.00, plus what
  StandardMaterial3D/lighting did for them.
- `src/view`: line-by-line ports of `remake/presentation/*` logic (fighter_view, stage_view,
  effects, HUD, all 2D screens, touch_controls, camera_rig...) against `src/gfx` instead of nodes.
- `src/audio`: a software mixer (sfx voices with the game's pan/volume rules from audio_director.gd,
  music streaming) -> AAudio (NDK, API 26+). Music decode: AMediaExtractor/AMediaCodec on Android.
- `src/video`: movies transcoded once (tools/prepare_assets.py, ffmpeg: .ogv -> .mp4 H.264 + AAC,
  same frame timing) and decoded with AMediaCodec to CPU YUV buffers -> GL textures (YUV->RGB shader).
- `src/platform/android`: NativeActivity (no Java), EGL, AInputQueue (multi-touch, gamepads via
  AKEYCODE_BUTTON_* / AMOTION axes), vibration (via JNI to Vibrator), app-specific storage for
  settings/progress JSON, lifecycle (pause on background, lost EGL context).
- `src/platform/host` (Windows, dev loop only): WGL window with a GL 4.x context using
  ARB_ES3_compatibility so the same `#version 300 es` shaders run, keyboard + XInput, PNG screenshots;
  music via an ffmpeg-decoded PCM cache. Lets me iterate and screenshot in seconds.
- Packaging: APK built with aapt2 + zipalign + apksigner (no Gradle), ABIs arm64-v8a + x86_64
  (emulator). Game data: copied by the user into the app's own storage, or bundled into a private APK.

## Order (vertical slices, each demonstrated by screenshots on host and emulator)
1. Host window + GL + PNG decoder + 2D batcher; GameFlow running at 60 Hz; boot -> title -> menu
   drawn with the game's pictures (psx_canvas / screen_canvas ports).
2. Android NativeActivity + EGL + touch/pads + APK on the emulator showing the same.
3. Fight: fighters (skinning shader), stage (panorama, floor), camera rig, frame interpolation.
4. Audio: sfx + music. 5. HUD, effects, all remaining screens. 6. Movies. 7. Touch controls
   (remake's layout) + pads + vibration. 8. Settings, save card, polish, performance on device.

## Risks
- Godot's built-in lighting/glow/shadows reproduced "close enough": needs a visual reference
  (Godot headless screenshots of the remake would be the oracle).
- MediaCodec quirks across devices; AAudio latency; GL context loss on Android.
- Scale: ~13k lines of presentation/app logic plus an engine layer.
