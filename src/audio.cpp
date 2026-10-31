#include "audio.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>
#include "miniaudio.h"

namespace audio {
namespace {

// Tracks are read from this folder, and sound effects from the other, both relative to the working directory
const char* const kTrackFolder = "BGM";
const char* const kEffectFolder = "SFX";
constexpr int BUS_COUNT = (int)Bus::Count;
constexpr int TRACK_COUNT = 2;             // one track each for the music and ambient buses
constexpr size_t EFFECT_COPIES = 4;        // how many times one effect can play at once
constexpr ma_uint64 DECODE_CHUNK = 4096;   // frames read from an effect file at a time

ma_engine engine;
bool engineReady = false;                  // the audio output is open
ma_sound_group buses[BUS_COUNT];           // every sound plays through its bus, so the bus level applies

// A looping track on the music or ambient bus
struct Track {
    ma_sound sound;
    bool ready = false;                    // `sound` holds a file that loaded
    std::string requested;                 // the name last asked for, empty when silent
};
Track tracks[TRACK_COUNT];                 // indexed like Bus: music, then ambient

// One copy of an effect: a sound that plays the effect's frames from its own position. The sound reads through
// its source, so both stay where they were made, and copies are held by pointer.
struct EffectCopy {
    ma_audio_buffer_ref source;
    ma_sound sound;
};

// One sound effect, decoded into memory the first time it is asked for. Its copies are made up front, and each
// play uses the next one in turn, so a play can start while an earlier one is still going.
struct Effect {
    std::vector<float> frames;                        // every frame, interleaved, in the engine's format
    std::vector<std::unique_ptr<EffectCopy>> copies;  // empty when the file is missing or cannot be decoded
    size_t next = 0;                                  // the copy the next play uses
};
std::map<std::string, Effect> effects;     // by name. A file is looked for once, however often it is asked for.

// The track slot for a bus, or -1 for a bus that holds no track
int trackSlot(Bus bus) {
    const int i = (int)bus;
    return i >= 0 && i < TRACK_COUNT ? i : -1;
}

// Opens a track file from a folder as a streamed sound on a bus. Windows takes the wide name, so any path works there.
ma_result openTrack(ma_sound* sound, const std::string& file, ma_sound_group* group) {
    const std::filesystem::path path = std::filesystem::path(kTrackFolder) / file;
    const ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
#ifdef _WIN32
    return ma_sound_init_from_file_w(&engine, path.c_str(), flags, group, nullptr, sound);
#else
    return ma_sound_init_from_file(&engine, path.string().c_str(), flags, group, nullptr, sound);
#endif
}

// Stops and frees a track, if one is loaded
void releaseTrack(Track& t) {
    if (t.ready) {
        ma_sound_stop(&t.sound);
        ma_sound_uninit(&t.sound);
        t.ready = false;
    }
}

// Decodes a whole effect file into frames in the engine's format. False when the file is missing or cannot be
// decoded. The decoder is read directly rather than through the engine's resource manager, which does not
// handle a file that fails to open.
bool decodeEffect(const std::string& file, std::vector<float>& frames) {
    const std::filesystem::path path = std::filesystem::path(kEffectFolder) / file;
    const ma_uint32 channels = ma_engine_get_channels(&engine);
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, channels, ma_engine_get_sample_rate(&engine));
    ma_decoder decoder;
#ifdef _WIN32
    const ma_result result = ma_decoder_init_file_w(path.c_str(), &config, &decoder);
#else
    const ma_result result = ma_decoder_init_file(path.string().c_str(), &config, &decoder);
#endif
    if (result != MA_SUCCESS) return false;

    std::vector<float> chunk(DECODE_CHUNK * channels);
    for (;;) {
        ma_uint64 read = 0;
        ma_decoder_read_pcm_frames(&decoder, chunk.data(), DECODE_CHUNK, &read);
        if (read == 0) break;                  // the end, or a file that stops decoding partway
        frames.insert(frames.end(), chunk.data(), chunk.data() + read * channels);
    }
    ma_decoder_uninit(&decoder);
    return !frames.empty();
}

// Makes the copies of an effect: each is a sound on the effects bus that plays the effect's frames
void openCopies(Effect& fx) {
    const ma_uint32 channels = ma_engine_get_channels(&engine);
    const ma_uint64 frameCount = fx.frames.size() / channels;
    for (size_t i = 0; i < EFFECT_COPIES; ++i) {
        auto copy = std::make_unique<EffectCopy>();
        if (ma_audio_buffer_ref_init(ma_format_f32, channels, fx.frames.data(), frameCount, &copy->source) != MA_SUCCESS) break;
        if (ma_sound_init_from_data_source(&engine, &copy->source, MA_SOUND_FLAG_NO_SPATIALIZATION,
                                           &buses[(int)Bus::Effects], &copy->sound) != MA_SUCCESS) break;
        fx.copies.push_back(std::move(copy));
    }
}

} // namespace

bool init() {
    if (engineReady) return true;
    const ma_engine_config config = ma_engine_config_init();
    if (ma_engine_init(&config, &engine) != MA_SUCCESS) return false;

    // Each bus is a sound group, which every sound on the bus plays through. The groups must be started
    // or nothing routed through them is heard.
    for (int i = 0; i < BUS_COUNT; ++i) {
        if (ma_sound_group_init(&engine, 0, nullptr, &buses[i]) != MA_SUCCESS) {
            for (int j = 0; j < i; ++j) ma_sound_group_uninit(&buses[j]);
            ma_engine_uninit(&engine);
            return false;
        }
        ma_sound_group_start(&buses[i]);
    }
    engineReady = true;
    return true;
}

void shutdown() {
    stopTracks();
    // Effect copies go before the engine they belong to
    for (auto& entry : effects) {
        for (auto& copy : entry.second.copies) ma_sound_uninit(&copy->sound);
    }
    effects.clear();
    if (engineReady) {
        for (int i = 0; i < BUS_COUNT; ++i) ma_sound_group_uninit(&buses[i]);
        ma_engine_uninit(&engine);
        engineReady = false;
    }
}

void setBus(Bus bus, int level, bool on) {
    const int index = (int)bus;
    if (!engineReady || index < 0 || index >= BUS_COUNT) return;
    const float gain = on ? std::clamp(level, 0, 100) / 100.0f : 0.0f;
    ma_sound_group_set_volume(&buses[index], gain);
}

void setGlobal(int level, bool on) {
    if (!engineReady) return;
    ma_engine_set_volume(&engine, on ? std::clamp(level, 0, 100) / 100.0f : 0.0f);
}

void playTrack(Bus bus, const std::string& file) {
    const int slot = trackSlot(bus);
    if (!engineReady || slot < 0) return;     // no output: nothing to play, and nothing to remember
    Track& t = tracks[slot];
    if (file == t.requested) return;
    releaseTrack(t);
    t.requested = file;
    if (file.empty()) return;

    // Each request is tried once. A missing file is not looked for again until the track changes.
    if (openTrack(&t.sound, file, &buses[slot]) != MA_SUCCESS) return;   // silent
    t.ready = true;
    ma_sound_set_looping(&t.sound, MA_TRUE);
    ma_sound_start(&t.sound);
}

void stopTracks() {
    for (Track& t : tracks) {
        releaseTrack(t);
        t.requested.clear();
    }
}

std::string trackPlaying(Bus bus) {
    const int slot = trackSlot(bus);
    return slot >= 0 && tracks[slot].ready ? tracks[slot].requested : std::string();
}

void playEffect(const std::string& file) {
    if (!engineReady || file.empty()) return;

    auto found = effects.find(file);
    if (found == effects.end()) {
        // The first request decodes the file and makes the copies. A missing or undecodable file makes none, so
        // it stays silent and is not looked for again.
        found = effects.emplace(file, Effect{}).first;
        if (decodeEffect(file, found->second.frames)) openCopies(found->second);
    }

    Effect& fx = found->second;
    if (fx.copies.empty()) return;            // silent
    EffectCopy& copy = *fx.copies[fx.next];
    fx.next = (fx.next + 1) % fx.copies.size();
    ma_sound_stop(&copy.sound);               // a copy still playing from its last use starts again
    ma_sound_seek_to_pcm_frame(&copy.sound, 0);
    ma_sound_start(&copy.sound);
}

} // namespace audio
