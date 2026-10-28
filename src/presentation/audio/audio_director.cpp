// remake/presentation/audio/audio_director.gd
#include "presentation/audio/audio_director.h"

#include <algorithm>
#include <cmath>

#include "content/fight_tables.h"
#include "content/json_file.h"
#include "core/fighter/character.h"
#include "core/sim_events.h"
#include "gd/io.h"
#include "platform/log.h"

namespace t3 {

namespace {

// Godot's linear_to_db (GDScript float: double).
double linear_to_db(double linear) { return std::log(linear) * 8.6858896380650365530225783783321; }

}  // namespace

void AudioDirector::Tween::step(double delta) {
    if (!running) return;
    elapsed += delta;
    double t = duration > 0.0 ? std::min(elapsed / duration, 1.0) : 1.0;
    if (method) method(from + (to - from) * t);
    if (t >= 1.0) running = false;
}

// The members whose default is an object: `var music := AudioStreamPlayer.new()`.
AudioDirector::AudioDirector(Mixer* mixer, const std::string& root) : mixer(mixer), root(root) {
    music = gd::make<AudioStreamPlayer>();
}

AudioDirector::~AudioDirector() { _exit_tree(); }

void AudioDirector::_ready() {
    // The buses (BUSES) are the mixer's own (Mixer::Bus); AudioServer.add_bus has no counterpart.
    // A pan pull-out of 0 folds both sides into one (SPEAKER OUT: MONO): the mixer's mono switch,
    // off until set_mono.
    music->mixer = mixer;
    music->bus = "Music";
    for (gd::Int i = 0; i < SPU_VOICES; ++i) {
        gd::Ref<AudioStreamPlayer> p = gd::make<AudioStreamPlayer>();
        p->mixer = mixer;
        p->voice = static_cast<int>(i);
        p->bus = "SFX";
        voices.append(p);
    }
    // Assets.has_group("music"): native builds have no asset manifest yet; the group is there when
    // its table is.
    music_available = gd::file_exists(gd::path_join(root, "music/music.json"));
    if (music_available) {
        gd::Json data = JsonFile::read(gd::path_join(root, "music/music.json"));
        tracks = data.get("tracks");
    }
    if (gd::file_exists(_sound_path("sounds.json"))) {
        gd::Json data = JsonFile::read(_sound_path("sounds.json"));
        banks = data.get("banks");
        volumes = JsonFile::ints(data.get("volumes"));
        voice_banks = data.get("voice_banks");
        for (const std::string& bank : BANK_NAMES.values()) {
            _preload_bank(bank);
        }
    }
}

void AudioDirector::_process(double delta) {
    if (_fade != nullptr) _fade->step(delta);
}

// The fight tables' sound codes and Mokujin remap (loaded once by FightContent).
void AudioDirector::use_tables(const gd::Ref<FightTables>& tables) {
    sound_codes = tables->sound_codes;
    mokujin_sounds = tables->effects->mokujin_sounds;
}

// SPEAKER OUT: MONO folds the mix to mono, STEREO leaves it.
void AudioDirector::set_mono(bool on) {
    if (on == mono) {
        return;
    }
    mono = on;
    if (mixer) mixer->set_mono(on);
}

void AudioDirector::_exit_tree() {
    for (const std::string& file : _requested.keys()) {
        _take_requested(file);
    }
    for (const std::string& file : _dropped.keys()) {
        _take_dropped(file);
    }
    // The downmix leaves the master bus.
    if (mixer && mono) mixer->set_mono(false);
}

// The voice banks and characters of the fight's fighters (SoundSelectBanks; players 1 and 2,
// the first entries of the arrays). The voice banks no fighter uses any more are dropped once both
// are known, so a bank that changes sides between two fights stays loaded.
void AudioDirector::set_fighters(const gd::PI32& costume_slots, const gd::PI32& char_ids) {
    gd::Int n = gd::mini(costume_slots.size(), fighter_banks.size());
    for (gd::Int player = 0; player < n; ++player) {
        const gd::Json& bank = voice_banks.is_object() ? voice_banks.get(gd::str(gd::Int(costume_slots[player]))) : gd::Json();
        fighter_banks[player] = bank.is_string() ? bank.str() : std::string("");
        fighter_chars[player] = char_ids[player];
    }
    _evict_unused_voices();
    for (const std::string& bank : fighter_banks) {
        _preload_bank(bank);
    }
}

// MusicPlay(track): starts a track, −1 stops the music. The track that is already the current one
// (every round start asks for the stage's) is not started again: only the volume goes back to full
// at once, FUN_8006BF80(0x7F, −1); a stop (a disc read, GameFlow.disc_read) forgets the track.
void AudioDirector::play_music(gd::Int track) {
    if (track >= 0 && track == _track && soundtrack == _version) {
        _prepared = -1;
        fade_music(static_cast<gd::Int>(VOLUME_MAX), 0);
        return;
    }
    play_track(track, soundtrack);
}

// A track in a given version (0 ARRANGE, 1 ARCADE): the Theater's sound player picks it.
void AudioDirector::play_track(gd::Int track, gd::Int version) {
    _prepared = -1;
    _track = track;
    _version = version;
    if (_fade != nullptr) {
        _fade->kill();
        _fade = nullptr;
    }
    music->stop();
    _music_level = 1.0;
    if (track < 0 || !music_available) {
        return;
    }
    if (!tracks.is_object() || !tracks.has(gd::str(track))) {
        return;
    }
    const gd::Json& variants = tracks[gd::str(track)];
    if (!variants.is_array() || variants.size() == 0) {
        return;
    }
    const gd::Json& v = variants[gd::clampi(version, 0, variants.size() - 1)];
    gd::Ref<AudioStream> stream = AudioStream::load(gd::path_join(root, "music/" + v["file"].str()));
    gd::Ref<AudioStreamOggVorbis> ogg = std::dynamic_pointer_cast<AudioStreamOggVorbis>(stream);
    if (ogg != nullptr) {
        bool loop = v["loop"].boolean();
        ogg->loop = loop;                    // the whole stream; a WAV carries its loop itself
    }
    music->stream = stream;
    double volume = v["volume"].num();
    _music_volume = volume / VOLUME_MAX;
    _apply_music_volume();
    music->play();
    Log::info("audio: music track " + gd::str(track) + " version " + gd::str(version) + " (" + v["file"].str() +
              (music->is_playing() ? ")" : ", does not play)"));
}

// MusicSetVolume(volume 0-127, frames): fades the music to `volume` over `frames`.
void AudioDirector::fade_music(gd::Int volume, gd::Int frames) {
    if (_fade != nullptr) {
        _fade->kill();
    }
    double target = std::clamp(static_cast<double>(volume) / VOLUME_MAX, 0.0, 1.0);
    if (frames <= 0) {
        _music_level = target;
        _apply_music_volume();
        return;
    }
    _fade = gd::make<Tween>();
    _fade->method = [this](double level) { _set_music_level(level); };
    _fade->from = _music_level;
    _fade->to = target;
    _fade->duration = static_cast<double>(frames) / 60.0;
}

void AudioDirector::_set_music_level(double level) {
    _music_level = level;
    _apply_music_volume();
}

void AudioDirector::_apply_music_volume() {
    music->set_volume_db(static_cast<float>(linear_to_db(std::max(_music_volume * _music_level, 0.0001))));
}

// Stops the music and every sound (before quitting: the audio server releases a playback on
// its next mix, so a stream still playing at exit is reported as leaked). The track is forgotten
// as on MUSIC_STOP: LOAD STATE's MusicPlay of the same fight's track must start it again.
void AudioDirector::stop_all() {
    play_track(-1, _version);
    for (const gd::Ref<AudioStreamPlayer>& p : voices) {
        p->stop();
    }
}

// SoundPlayFighter(player, code, attenuation).
void AudioDirector::play_code(gd::Int code, gd::Int player, gd::Int attenuation) {
    player = gd::clampi(player, 0, 1);
    code &= 0xFFFF;
    if (fighter_chars[player] == Character::MOKUJIN) {
        code = _mokujin(code);
    }
    gd::Int bank = code >> 14;
    if (bank == 0) {
        return;
    }
    gd::Int voice = (code >> 10) & 0xF;
    if ((attenuation & 0x80) != 0) {
        voice += ECHO_VOICES_FROM;
    } else if (voice >= PLAYER_VOICES_FROM) {
        voice += player;
    }
    gd::Int level = !volumes.is_empty() ? gd::shr(gd::Int(volumes[(code >> 6) & 0xF]), attenuation & 0x1F) : 127;
    _key_on(voice, bank, fighter_banks[player], code, level);
}

// SoundPlaySystem(id): entry `id` of the sound table on its own voice and bank.
void AudioDirector::play_system(gd::Int id) {
    if (id < 0 || id >= sound_codes.size()) {
        return;
    }
    gd::Int code = sound_codes[id];
    gd::Int bank = code >> 14;
    if (bank == 0) {
        return;
    }
    gd::Int level = !volumes.is_empty() ? gd::Int(volumes[(code >> 6) & 0xF]) : 127;
    // Bank 3 would be VAB 2, the voice cache's first slot; the game never reaches it here: every
    // SoundPlaySystem id (FUN_80040E98: the announcer, the continue, the sound scripts' 5-9) is
    // in VAB 1 (sound.md).
    _key_on((code >> 10) & 0xF, bank, fighter_banks[0], code, level);
}

// SoundStopFighter(player, code): keys off the code's voice.
void AudioDirector::stop_code(gd::Int code, gd::Int player) {
    if ((code & 0xC000) == 0) {
        return;
    }
    gd::Int voice = (code & 0x3C00) >> 10;
    if (voice > 5) {
        voice += gd::clampi(player, 0, 1);
    }
    key_off(voice);
}

void AudioDirector::key_off(gd::Int voice) {
    if (voice >= 0 && voice < voices.size()) {
        voices[voice]->stop();
    }
}

// FUN_8004B920: every voice off but the announcer's (and the music ones with `keep_music`).
void AudioDirector::voices_off(bool keep_music) {
    for (gd::Int v = 0; v < SPU_VOICES; ++v) {
        if (VOICES_KEPT.has(v) || (keep_music && VOICES_KEPT_MUSIC.has(v))) {
            continue;
        }
        key_off(v);
    }
}

// The sound and music requests of a simulation step (from its `from`-th event: a frame
// finished after a movie handles only what came after the movie).
void AudioDirector::handle(const gd::Ref<SimEvents>& events, gd::Int from) {
    _collect_dropped();
    gd::Int n = events->items.size();
    for (gd::Int i = from; i < n; ++i) {
        const gd::Ref<SimEvents::Event>& e = events->items[i];
        switch (e->kind) {
            case SimEvents::Kind::SOUND:
                play_code(e->a, e->b, e->c);
                break;
            case SimEvents::Kind::SOUND_STOP:
                stop_code(e->a, e->b);
                break;
            case SimEvents::Kind::SYSTEM_SOUND:
                play_system(e->a);
                break;
            case SimEvents::Kind::VOICE_OFF:
                key_off(e->a);
                break;
            case SimEvents::Kind::VOICES_OFF:
                voices_off(e->a != 0);
                break;
            case SimEvents::Kind::MUSIC:
                play_music(e->a);
                break;
            case SimEvents::Kind::MUSIC_PREPARE:
                // MusicPlay(track, 1) only loads the stream: it is heard from the volume call
                // that FUN_8006BCC8 makes (a disc read or a MusicPlay in between drops it).
                play_music(-1);
                _prepared = e->a;
                break;
            case SimEvents::Kind::MUSIC_TRACK:
                play_track(e->a, e->b);
                break;
            case SimEvents::Kind::MUSIC_STOP:
                play_music(-1);
                break;
            case SimEvents::Kind::MUSIC_VOLUME:
                if (_prepared >= 0) {
                    play_music(_prepared);
                }
                fade_music(e->a, e->b);
                break;
            default:
                break;
        }
    }
}

// FUN_80075830: Mokujin's replacement for the codes of some sound ids.
gd::Int AudioDirector::_mokujin(gd::Int code) {
    for (const gd::PI32& pair : mokujin_sounds) {
        if (pair[0] < sound_codes.size() && sound_codes[pair[0]] == code) {
            return pair[1] & 0xFFFF;
        }
    }
    return code;
}

// SsUtKeyOffV, then SsUtKeyOnV: the voice stops even when nothing plays on it (no bank or
// tone); voices from 24 on do not exist.
void AudioDirector::_key_on(gd::Int voice, gd::Int bank, const std::string& fighter_bank, gd::Int code, gd::Int level) {
    if (voice >= voices.size()) {
        return;
    }
    gd::Ref<AudioStreamPlayer> p = voices[voice];
    p->stop();
    std::string name = bank == BANK_FIGHTER ? fighter_bank : BANK_NAMES.get(bank, "");
    if (name.empty() || !banks.is_object() || !banks.has(name)) {
        return;
    }
    const gd::Json& tones = banks[name].get("tones");
    std::string key = gd::str((code >> 4) & 3) + "_" + gd::str(code & 0xF);
    if (!tones.is_object() || !tones.has(key)) {
        return;
    }
    const gd::Json& tone = tones[key];
    p->stream = _stream(tone["file"].str());
    p->bus = BANK_BUSES.get(bank, "SFX");
    double gain = tone["gain"].num();
    p->volume_db = static_cast<float>(linear_to_db(std::max(gain * static_cast<double>(level) / VOLUME_MAX, 0.0001)));
    p->play();
    key_ons += 1;
    Log::debug("audio: voice " + gd::str(voice) + " " + tone["file"].str() + " level " + gd::str(level));
}

// A tone's stream: the one requested ahead (`_preload_bank`) or loaded now.
gd::Ref<AudioStream> AudioDirector::_stream(const std::string& file) {
    if (!_streams.has(file)) {
        _revive(file);
        gd::Ref<AudioStream> stream = _requested.has(file) ? _take_requested(file) : nullptr;
        _streams.set(file, stream != nullptr ? stream : AudioStream::load(_sound_path(file)));
    }
    return _streams.at(file);
}

// The result of a background load (`_preload_bank`), waiting for it if need be. Every request is
// taken: the loader keeps a request nobody takes until the end, and it leaks at exit.
// Native: there is no background loader (ResourceLoader.load_threaded_get): the file loads now.
gd::Ref<AudioStream> AudioDirector::_take_requested(const std::string& file) {
    _requested.erase(file);
    return AudioStream::load(_sound_path(file));
}

// Native: a dropped load holds nothing to take (ResourceLoader.load_threaded_get).
void AudioDirector::_take_dropped(const std::string& file) {
    _dropped.erase(file);
}

std::string AudioDirector::_sound_path(const std::string& file) {
    return gd::path_join(root, "sounds/" + file);
}

// A dropped load wanted again becomes a request.
void AudioDirector::_revive(const std::string& file) {
    if (_dropped.has(file)) {
        _dropped.erase(file);
        _requested.set(file, true);
    }
}

// Takes the dropped loads that have finished (natively every load has finished:
// ResourceLoader.load_threaded_get_status is never THREAD_LOAD_IN_PROGRESS).
void AudioDirector::_collect_dropped() {
    if (_dropped.is_empty()) {
        return;
    }
    for (const std::string& file : _dropped.keys()) {
        _take_dropped(file);
    }
}

// The sound files of a bank's tones.
gd::Arr<std::string> AudioDirector::_bank_files(const std::string& bank) {
    gd::Arr<std::string> files;
    if (banks.is_object() && banks.has(bank)) {
        const gd::Json& tones = banks[bank].get("tones");
        if (tones.is_object()) {
            for (const auto& kv : tones.members()) {
                files.append(kv.second["file"].str());
            }
        }
    }
    return files;
}

// Loads a bank's tones ahead of their first key-on, which would otherwise load and decode them
// in the middle of a simulation step: in the background, except on the web, where threads are not
// a given and the bank is loaded now.
// Native: no background loader (WAV tones are only read and parsed), so the web's branch.
void AudioDirector::_preload_bank(const std::string& bank) {
    for (const std::string& file : _bank_files(bank)) {
        _revive(file);
        if (_streams.has(file) || _requested.has(file)) {
            continue;
        }
        _stream(file);
    }
}

// Drops the tones of the voice banks that no fighter of the fight uses any more: the common banks
// and the two fighters' voice banks stay.
void AudioDirector::_evict_unused_voices() {
    gd::Dict<std::string, bool> keep;
    gd::Arr<std::string> kept{fighter_banks[0], fighter_banks[1]};
    kept.append_array(BANK_NAMES.values());
    for (const std::string& bank : kept) {
        for (const std::string& file : _bank_files(bank)) {
            keep.set(file, true);
        }
    }
    for (const std::string& file : _streams.keys()) {
        if (!keep.has(file)) {
            _streams.erase(file);
        }
    }
    for (const std::string& file : _requested.keys()) {
        if (!keep.has(file)) {
            _requested.erase(file);
            _dropped.set(file, true);
        }
    }
}

}  // namespace t3
