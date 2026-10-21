# Tekken 3 native Android port: plan (stage 1 = simulation)

Source: n0madic/Tekken3Remake (Godot 4.7, typed GDScript, PolyForm Noncommercial).
`remake/core` = 26,986 lines: deterministic 60 Hz integer simulation (4.12 fixed point,
GTE rounding), plain RefCounted classes, no Nodes/scene tree, no floats (grep: 0), no await,
2 signals, 34 lambdas, 186 `match`, 571 PackedInt32Array uses, 157 typed Array[T], 13 Dictionary.
Output leaves the sim only as per-frame SimEvents. `content/` (2.8k lines) loads converted
assets (JSON, gzip). Tests (75 suites) replay golden traces produced by running the original
game's code (Japan Rev.1) in a Unicorn MIPS + software GTE harness and compare every fighter
field, AI record, camera, round globals and sound/vibration calls every frame
(fight traces: 40 scenarios; flow traces: whole game from boot, 8+ scenarios; pose, enbu,
attachments, vibration, back colour traces).

Goal: a native Android game (C++ NDK, no Godot) whose simulation is bit-exact with the
original, proven by the same golden traces.

## Decisions

1. Separate repo `t3native/` (CMake). One C++20 static library `t3core` built for host
   (MinGW g++ 15 on Windows, fast test loop) and Android arm64-v8a (NDK r26). `-fwrapv`.
2. File-for-file translation of `remake/core/**.gd` into `core/**.{h,cpp}` with the same class
   and member names, so each C++ file can be reviewed against its GDScript source.
   Not a reinterpretation: the GDScript is already proven against the original.
3. Value semantics rulebook (the main risk):
   - GDScript `int` (64-bit) -> `int64_t` everywhere, including intermediate arithmetic; the
     code already calls explicit wrap helpers (Fx.s16, Fx.w32, ...) where the original wraps.
   - `PackedInt32Array` -> class `PI32` holding `int32_t`; stores truncate exactly like Godot
     (int64 -> int32 two's complement). `PackedByteArray` -> `PBytes` with Godot's decode_* /
     encode_* semantics.
   - `/` and `%` on ints: GDScript truncates toward zero like C; `>>` arithmetic. Division by
     zero: check what Godot does and mirror it.
   - Reference vs value: RefCounted objects -> `std::shared_ptr` (aliasing preserved);
     Array/Dictionary are reference types in GDScript -> shared ownership too; Packed arrays
     are copy-on-write values in Godot 4 (TO VERIFY) -> plain value types. `duplicate()` ->
     explicit copies (shallow vs deep as in Godot).
   - `match` -> `switch` or if-chains (GDScript match does not fall through).
   - Dictionaries iterate in insertion order -> ordered map wrapper if iteration order matters.
4. Content: port `content/` loaders; JSON via a small vendored parser (or own minimal one),
   gzip via zlib (in the NDK; MinGW ships it). Data stays in the user's converted
   `remake/imported/` (never in the repo); the Python converter is reused unchanged.
5. Tests: C++ test runner reproducing `tests/support` (FightTrace, FighterCompare, AiCompare,
   FightCalls, flow trace reader) and the trace suites, run on host; later also on device
   (adb) to prove arm64 equality.
6. Order (vertical slices, each closed by its traces before the next):
   a. math (Fx, Gte, CRand) + unit tests against the Python ports;
   b. content loaders + motion/pose -> pose traces;
   c. fighter, combat, fight, camera, effects -> fight traces (human vs human scenarios);
   d. ai -> cpu_ fight traces; e. ball, force, scripted -> their traces;
   f. game flow (8.4k lines) -> flow traces.
   Then stage 2: renderer (GLES3), audio (Oboe), movies (MediaCodec), input/touch, Activity.
7. Translation is done by me (Claude) directory by directory; a bounded subagent may translate
   leaf files, but every file is reviewed against its source and nothing counts as done until
   its traces pass.

## Risks I see

- Silent semantic drift (int widths, Packed vs Array semantics, copy vs alias) that only some
  traces exercise. Mitigation: traces + randomised differential runs against Godot headless.
- The trace pipeline is rebuilt by me (the author's work/ghidra scripts are not public);
  my Ghidra run names 772 PsyQ functions in the library range vs the author's "929".
  The harness stubs library code by name, so a naming gap could change traces.
  Mitigation: run the author's own GDScript tests (Godot headless) on my traces first.
- Scale: ~27k lines of core before anything is playable.
