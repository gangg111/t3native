#pragma once
// remake/presentation/audio/audio_director.gd
// Plays the simulation's and the screens' sound requests on separate buses (remake-plan.md#audio):
// music tracks (from `imported/music/music.json`, ARRANGE or ARCADE, with the track's volume;
// silent in a build without music) and the game's 16-bit sound codes (sound.md#sound-code) on
// emulated SPU voices, so that a sound replaces the one playing on its voice as in the game.
// SPEAKER OUT's MONO (the options) folds the whole mix down to mono on the master bus.
//
// Native: Godot's AudioServer, AudioStreamPlayer and streams are the mixer's (audio/mixer.h); the
// node's `_ready` is called by the owner after the constructor, `_exit_tree` by the destructor, and
// `_process(delta)` (the owner, once a step) runs the music fade that a Tween ran in Godot.
#include <functional>
#include <string>

#include "audio/mixer.h"
#include "gd/gd.h"
#include "gd/json.h"

namespace t3 {

class FightTables;
class SimEvents;

class AudioDirector {
public:
    static inline const gd::Arr<std::string> BUSES{"Music", "SFX", "Voices", "System"};
    static constexpr gd::Int SPU_VOICES = 24;               // SsUtKeyOnV ignores voices from 24 on
    static constexpr gd::Int ECHO_VOICES_FROM = 16;         // attenuation bit 0x80 moves a sound 16 voices up
    static constexpr gd::Int PLAYER_VOICES_FROM = 6;        // voices above 5 are offset by the player number
    static constexpr double VOLUME_MAX = 127.0;
    static constexpr gd::Int BANK_COMBAT = 1;
    static constexpr gd::Int BANK_SYSTEM = 2;
    static constexpr gd::Int BANK_FIGHTER = 3;
    static inline const gd::Dict<gd::Int, std::string> BANK_NAMES = [] {
        gd::Dict<gd::Int, std::string> d;
        d.set(BANK_COMBAT, "combat");
        d.set(BANK_SYSTEM, "system");
        return d;
    }();
    static inline const gd::Dict<gd::Int, std::string> BANK_BUSES = [] {
        gd::Dict<gd::Int, std::string> d;
        d.set(BANK_COMBAT, "SFX");
        d.set(BANK_SYSTEM, "System");
        d.set(BANK_FIGHTER, "Voices");
        return d;
    }();
    // FUN_8004B920: voices it leaves on (the announcer's), and with `keep_music` these as well.
    static inline const gd::Arr<gd::Int> VOICES_KEPT{2, 3};
    static inline const gd::Arr<gd::Int> VOICES_KEPT_MUSIC{1, 6, 7};

    // The Tween of `fade_music` (create_tween().tween_method(method, from, to, duration), linear).
    class Tween {
    public:
        std::function<void(double)> method;
        double from = 0.0, to = 0.0, duration = 0.0, elapsed = 0.0;
        bool running = true;
        void kill() { running = false; }
        // One process frame of `delta` seconds: the method gets the interpolated value.
        void step(double delta);
    };

    Mixer* mixer = nullptr;              // native: the AudioServer
    std::string root;                    // native: the converted assets (Assets.path)

    gd::Ref<AudioStreamPlayer> music;
    gd::Arr<gd::Ref<AudioStreamPlayer>> voices;
    gd::Json tracks;
    gd::Json banks;                      // bank name → tones ("<program>_<tone>" → file, gain)
    gd::PI32 volumes;                    // key-on volume by code bits 6-9
    gd::Json voice_banks;                // costume slot → voice bank name
    gd::PI32 sound_codes;                // g_soundTable (SoundPlaySystem, the Mokujin remap)
    gd::Arr<gd::PI32> mokujin_sounds;
    gd::Arr<std::string> fighter_banks{"", ""};
    gd::PI32 fighter_chars{-1, -1};
    gd::Int soundtrack = 0;              // 0 ARRANGE, 1 ARCADE (the BGM option)
    bool music_available = false;
    double _music_volume = 1.0;          // the track's own volume
    double _music_level = 1.0;           // the fade level (MusicSetVolume)
    gd::Int _prepared = -1;              // the track MusicPlay(track, 1) loaded, silent until the volume call
    gd::Int _track = -1;                 // the track MusicPlay started (0x800A09C8); −1 once a stop forgot it
    gd::Int _version = -1;               // and its version (the BGM option)
    gd::Ref<Tween> _fade;
    gd::Dict<std::string, gd::Ref<AudioStream>> _streams;   // tone file → stream
    gd::Dict<std::string, bool> _requested;   // tone file → true: a background load is under way (_preload_bank)
    // Background loads of tones no fighter needs any more: taken (and thrown away) once finished, so
    // that dropping them never waits for the decoder.
    gd::Dict<std::string, bool> _dropped;
    bool mono = false;                   // SPEAKER OUT: MONO
    // _downmix (AudioEffectStereoEnhance, pan_pullout 0): the mixer's mono switch (Mixer::set_mono).

    gd::Int key_ons = 0;                 // native: the tones keyed on so far (for the app's log)

    AudioDirector(Mixer* mixer, const std::string& root);
    ~AudioDirector();
    AudioDirector(const AudioDirector&) = delete;
    AudioDirector& operator=(const AudioDirector&) = delete;

    void _ready();
    void _process(double delta);
    // The fight tables' sound codes and Mokujin remap (loaded once by FightContent).
    void use_tables(const gd::Ref<FightTables>& tables);
    // SPEAKER OUT: MONO folds the mix to mono, STEREO leaves it.
    void set_mono(bool on);
    void _exit_tree();
    void set_fighters(const gd::PI32& costume_slots, const gd::PI32& char_ids);
    void play_music(gd::Int track);
    void play_track(gd::Int track, gd::Int version);
    void fade_music(gd::Int volume, gd::Int frames);
    void _set_music_level(double level);
    void _apply_music_volume();
    void stop_all();
    void play_code(gd::Int code, gd::Int player, gd::Int attenuation);
    void play_system(gd::Int id);
    void stop_code(gd::Int code, gd::Int player);
    void key_off(gd::Int voice);
    void voices_off(bool keep_music);
    void handle(const gd::Ref<SimEvents>& events, gd::Int from = 0);
    gd::Int _mokujin(gd::Int code);
    void _key_on(gd::Int voice, gd::Int bank, const std::string& fighter_bank, gd::Int code, gd::Int level);
    gd::Ref<AudioStream> _stream(const std::string& file);
    gd::Ref<AudioStream> _take_requested(const std::string& file);
    void _take_dropped(const std::string& file);
    std::string _sound_path(const std::string& file);
    void _revive(const std::string& file);
    void _collect_dropped();
    gd::Arr<std::string> _bank_files(const std::string& bank);
    void _preload_bank(const std::string& bank);
    void _evict_unused_voices();
};

}  // namespace t3
