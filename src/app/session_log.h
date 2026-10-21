#pragma once
// Native: save states by re-simulation. The game is deterministic: a GameFlow made with the same
// seed, rules and progress, given the same inputs, steps to the same state (the flow traces show
// it byte for byte). So the app logs everything that crosses into the flow from the session's
// start: before each step the pads, Escape, the one-or-two-controllers mask and the movie's end;
// what the flow's callbacks returned (the card read, the save write, SYSTEM OPTION's step); and
// what the app changed between or inside steps (the rules, another card's progress). A save
// state is that log and a checksum of the state it reaches; loading builds a new flow and steps
// it through the log without drawing, and takes it only when the checksum agrees.
//
// The core never reads the clock, the settings or the platform; its callbacks are the only other
// way in, so nothing else needs logging.
#include <cstdint>
#include <string>
#include <vector>

#include "gd/gd.h"

namespace t3 {

class GameFlow;

class SessionLog {
public:
    static constexpr uint32_t MAGIC = 0x53533354;   // "T3SS"
    static constexpr uint32_t VERSION = 1;

    // What a step had before it ran (flags), and after (SAVE_FAILED).
    enum Flag : uint8_t { ESCAPE = 1, ONE_PAD = 2, SAVE_FAILED = 4 };
    struct Step {
        uint32_t pad0 = 0, pad1 = 0;
        uint8_t flags = 0;
        int8_t movie_result = 0;
    };
    // Callback results (inside a step) and the app's own changes (inside or between steps).
    enum Kind : uint8_t { CARD_READ = 1, SAVE_WRITE = 2, SYSTEM_STEP = 3, RULES = 4, SWITCH_CARD = 5 };
    struct Event {
        uint32_t step = 0;     // the step it belongs to (between steps: the step after it)
        uint8_t kind = 0;
        bool inside = false;   // made during the step (by a callback), else before it
        int32_t value = 0;
        std::vector<uint8_t> bytes;
    };

    // The start.
    int64_t seed = 0;
    bool movies = false;
    bool fixes = false;                  // the rules' gameplay fixes
    std::vector<uint8_t> progress;       // the progress block after the card was read
    std::vector<Step> steps;
    std::vector<Event> events;
    uint64_t check = 0;                  // a save state: the checksum after its last step

    // The pre-step inputs of `flow` with these pads (the step's entry).
    static Step capture(const GameFlow& flow, const gd::PI32& pads);
    // Puts a step's entry back on `flow` (the replay): its flags and the native pause items.
    static void prepare(GameFlow& flow, const Step& step);
    static gd::PI32 pads(const Step& step) { return gd::PI32{gd::Int(step.pad0), gd::Int(step.pad1)}; }

    // A checksum of the flow's state: the memory blocks, the random generators, the frame counter
    // and the fighters' places and health.
    static uint64_t checksum(const GameFlow& flow);

    // A copy of the bytes of a block's packed array, and back in place (same size).
    static std::vector<uint8_t> bytes_of(const gd::PBytes& bytes);
    static void put_bytes(const gd::PBytes& into, const std::vector<uint8_t>& bytes);

    std::vector<uint8_t> serialize() const;
    // False (and the log unchanged) when the bytes are not a save state of this version.
    bool parse(const std::vector<uint8_t>& data);
};

}  // namespace t3
