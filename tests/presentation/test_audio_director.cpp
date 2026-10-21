// remake/tests/presentation/test_audio_director.gd: AudioDirector plays sound codes on emulated SPU
// voices as SoundPlayFighter does: the voice is keyed off before the key-on, so a code with no tone
// behind it still cuts the old sound. (Without a scene tree: the director's _ready is called
// directly; the mixer has no audio thread, so its commands only queue.)
//
// Not ported: test_dropped_loads_do_not_block (the native build has no background loader, as the
// web build, where the GDScript test returns early).
#include "audio/mixer.h"
#include "core/sim_events.h"
#include "gd/io.h"
#include "presentation/audio/audio_director.h"
#include "support/test_suite.h"

using namespace t3;

namespace {
constexpr gd::Int COMBAT_TONE = 0x4000 | 8 << 10 | 15 << 6;   // combat bank, voice 8, program 0 tone 0
constexpr gd::Int MISSING_TONE = COMBAT_TONE | 3 << 4 | 15;   // program 3 tone 15: not in the bank
}  // namespace

T3_TEST(audio_director, a_silent_code_keys_its_voice_off) {
    if (!t.require(t3test::imported("sounds/sounds.json"))) return;
    Mixer mixer;
    AudioDirector director(&mixer, t3test::imported(""));
    director._ready();
    director.play_code(COMBAT_TONE, 0, 0);
    t.expect(director.voices[8]->is_playing(), "the combat tone plays on voice 8");
    director.play_code(MISSING_TONE, 0, 0);
    t.expect(!director.voices[8]->is_playing(), "a code without a tone stops voice 8");
    director.stop_all();
}

// The first hit or voice of a fight must not load its tone in the middle of a step: the common
// banks are requested with the director and a fighter's voice bank when the fighter is set, and the
// voice banks of earlier fights are dropped.
T3_TEST(audio_director, tones_are_loaded_ahead_of_their_key_on) {
    if (!t.require(t3test::imported("sounds/sounds.json"))) return;
    Mixer mixer;
    AudioDirector director(&mixer, t3test::imported(""));
    director._ready();
    gd::Int common = 0;
    for (const std::string& bank : AudioDirector::BANK_NAMES.values()) {
        common += director._bank_files(bank).size();
    }
    t.expect_equal(director._streams.size() + director._requested.size(), common, "the combat and system banks");
    director.set_fighters(gd::PI32{0}, gd::PI32{0});
    gd::Arr<std::string> first = director._bank_files(director.fighter_banks[0]);
    t.expect_equal(director._streams.size() + director._requested.size(), common + first.size(), "and the fighter's voice bank");
    for (const std::string& file : first) {
        t.expect(director._stream(file) != nullptr, "the tone " + file + " is there when it is keyed on");
    }
    director.set_fighters(gd::PI32{10}, gd::PI32{9});
    gd::Arr<std::string> second = director._bank_files(director.fighter_banks[0]);
    t.expect(director.fighter_banks[0] != "voice_00", "another voice bank");
    t.expect_equal(director._streams.size() + director._requested.size(), common + second.size(), "the earlier voice bank is dropped");
    for (const std::string& file : first) {
        t.expect(!director._streams.has(file) && !director._requested.has(file), file + " is gone");
    }
}

// Two fighters changing sides between fights keep their voice banks: they are dropped only once
// both fighters are known.
T3_TEST(audio_director, voice_banks_that_change_sides_stay) {
    if (!t.require(t3test::imported("sounds/sounds.json"))) return;
    Mixer mixer;
    AudioDirector director(&mixer, t3test::imported(""));
    director._ready();
    director.set_fighters(gd::PI32{0, 10}, gd::PI32{0, 9});
    gd::Arr<std::string> files = director._bank_files(director.fighter_banks[0]);
    files.append_array(director._bank_files(director.fighter_banks[1]));
    t.expect(director.fighter_banks[0] != director.fighter_banks[1], "two voice banks");
    for (const std::string& file : files) {
        director._stream(file);
    }
    director.set_fighters(gd::PI32{10, 0}, gd::PI32{9, 0});
    for (const std::string& file : files) {
        t.expect(director._streams.has(file), file + " stays loaded");
    }
}

// SPEAKER OUT: MONO folds the master bus down to mono; the downmix leaves with the director.
// (The bus effect is the mixer's mono switch.)
T3_TEST(audio_director, mono_speaker_out) {
    Mixer mixer;
    {
        AudioDirector director(&mixer, t3test::imported(""));
        director._ready();
        t.expect(!mixer.mono(), "off in stereo");
        director.set_mono(true);
        t.expect(mixer.mono(), "on in mono");
        director.set_mono(false);
        t.expect(!mixer.mono(), "off again");
        director.set_mono(true);
    }
    t.expect(!mixer.mono(), "removed with the director");
}

// MusicPlay(track, 1) only loads the stream: the attract demonstration's prepared track must not
// sound before its scene (it is stopped and played again there); it is heard from the volume call.
T3_TEST(audio_director, a_prepared_track_is_silent_until_the_volume_call) {
    if (!t.require(t3test::imported("music/music.json"), "no music group")) return;
    Mixer mixer;
    AudioDirector director(&mixer, t3test::imported(""));
    director._ready();
    gd::Ref<SimEvents> events = gd::make<SimEvents>();
    events->add(SimEvents::Kind::MUSIC_PREPARE, -1, 0);
    director.handle(events);
    t.expect(!director.music->is_playing(), "prepared, not playing");
    gd::Ref<SimEvents> stop = gd::make<SimEvents>();
    stop->add(SimEvents::Kind::MUSIC_STOP, -1);
    stop->add(SimEvents::Kind::MUSIC_VOLUME, -1, 0x7F, -1);
    director.handle(stop);
    t.expect(!director.music->is_playing(), "a stop forgets the prepared track");
    director.handle(events);
    gd::Ref<SimEvents> ready = gd::make<SimEvents>();
    ready->add(SimEvents::Kind::MUSIC_VOLUME, -1, 0x7F, -1);
    director.handle(ready);
    t.expect(director.music->is_playing(), "the volume call starts it");
    director.stop_all();
}

// MusicPlay of the track already playing (each round start, the same fight after CONTINUE) does not
// start it again, FUN_8006BF80(0x7F, −1) only brings the volume back at once; a stop (every disc
// read) forgets the track, so the next MusicPlay starts it from the beginning.
T3_TEST(audio_director, the_current_track_is_not_restarted) {
    if (!t.require(t3test::imported("music/music.json"), "no music group")) return;
    Mixer mixer;
    AudioDirector director(&mixer, t3test::imported(""));
    director._ready();
    gd::Int track = std::stoll(director.tracks.members()[0].first);
    director.play_music(track);
    t.expect(director.music->is_playing(), "the first MusicPlay starts the track");
    director.fade_music(0, 0);
    director.music->stop();            // stands for a position that is no longer the beginning
    director.play_music(track);
    t.expect(!director.music->is_playing(), "the same track is not started again");
    t.expect_equal(director._music_level, 1.0, "the volume is back at once");
    director.play_music(-1);
    director.play_music(track);
    t.expect(director.music->is_playing(), "after a stop the track starts again");
    director.stop_all();
}

// Native: MusicSetVolume's fade (a Tween in Godot) reaches its level after `frames` / 60 seconds.
T3_TEST(audio_director, the_music_fade_takes_its_frames) {
    Mixer mixer;
    AudioDirector director(&mixer, t3test::imported(""));
    director._ready();
    director.fade_music(0, 30);
    for (int i = 0; i < 15; ++i) director._process(1.0 / 60.0);
    t.expect(std::abs(director._music_level - 0.5) < 1e-9, "half way after 15 of 30 frames");
    for (int i = 0; i < 20; ++i) director._process(1.0 / 60.0);
    t.expect_equal(director._music_level, 0.0, "at the target after them");
}
