#include "audio.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <vector>
#include "miniaudio.h"

namespace audio {
namespace {
const char* const kTrackFolder = "BGM";
const char* const kEffectFolder = "SFX";
constexpr int BUS_COUNT = (int)Bus::Count;
constexpr int TRACK_COUNT = 2;
constexpr size_t EFFECT_COPIES = 4;
constexpr ma_uint64 DECODE_CHUNK = 4096;

ma_engine engine;
bool engineReady = false;
ma_sound_group buses[BUS_COUNT];

struct Track {
    ma_sound sound;
    bool ready = false;
    bool started = false;   // false for a track opened ahead by preloadTrack and not yet asked to play
    std::string requested;
};
Track tracks[TRACK_COUNT];

struct EffectCopy {
    ma_audio_buffer_ref source;
    ma_sound sound;
};

struct Effect {
    std::vector<float> frames;
    std::vector<std::unique_ptr<EffectCopy>> copies;
    size_t next = 0;
};
std::map<std::string, Effect> effects;

int trackSlot(Bus bus) {
    const int i = (int)bus;
    return i >= 0 && i < TRACK_COUNT ? i : -1;
}

ma_result openTrack(ma_sound* sound, const std::string& file, ma_sound_group* group) {
    const std::filesystem::path path = std::filesystem::path(kTrackFolder) / file;
    const ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
#ifdef _WIN32
    return ma_sound_init_from_file_w(&engine, path.c_str(), flags, group, nullptr, sound);
#else
    return ma_sound_init_from_file(&engine, path.string().c_str(), flags, group, nullptr, sound);
#endif
}

void releaseTrack(Track& t) {
    if (t.ready) {
        ma_sound_stop(&t.sound);
        ma_sound_uninit(&t.sound);
        t.ready = false;
    }
    t.started = false;
}

// Opens the bus's track, ready to play from its start, without starting it
void openRequested(Bus bus, const std::string& file) {
    const int slot = trackSlot(bus);
    Track& t = tracks[slot];
    releaseTrack(t);
    t.requested = file;
    if (file.empty()) return;
    if (openTrack(&t.sound, file, &buses[slot]) != MA_SUCCESS) return;
    t.ready = true;
    ma_sound_set_looping(&t.sound, MA_TRUE);
}

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
        if (read == 0) break;
        frames.insert(frames.end(), chunk.data(), chunk.data() + read * channels);
    }
    ma_decoder_uninit(&decoder);
    return !frames.empty();
}

void openCopies(Effect& fx, Bus bus) {
    const ma_uint32 channels = ma_engine_get_channels(&engine);
    const ma_uint64 frameCount = fx.frames.size() / channels;
    for (size_t i = 0; i < EFFECT_COPIES; ++i) {
        auto copy = std::make_unique<EffectCopy>();
        if (ma_audio_buffer_ref_init(ma_format_f32, channels, fx.frames.data(), frameCount, &copy->source) != MA_SUCCESS) break;
        if (ma_sound_init_from_data_source(&engine, &copy->source, MA_SOUND_FLAG_NO_SPATIALIZATION,
                                           &buses[(int)bus], &copy->sound) != MA_SUCCESS) break;
        fx.copies.push_back(std::move(copy));
    }
}
}

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
    if (!engineReady || slot < 0) return;
    Track& t = tracks[slot];
    if (file != t.requested) openRequested(bus, file);
    if (t.ready && !t.started) {
        ma_sound_start(&t.sound);
        t.started = true;
    }
}

void preloadTrack(Bus bus, const std::string& file) {
    const int slot = trackSlot(bus);
    if (!engineReady || slot < 0 || file == tracks[slot].requested) return;
    openRequested(bus, file);
}

void stopTracks() {
    for (Track& t : tracks) {
        releaseTrack(t);
        t.requested.clear();
    }
}

static Effect& effectFor(const std::string& file, Bus bus) {
    if (bus != Bus::Interface) bus = Bus::Effects;

    const std::string key = file + (bus == Bus::Interface ? "|ui" : "");
    auto found = effects.find(key);
    if (found == effects.end()) {
        found = effects.emplace(key, Effect{}).first;
        if (decodeEffect(file, found->second.frames)) openCopies(found->second, bus);
    }
    return found->second;
}

void preloadEffect(const std::string& file, Bus bus) {
    if (engineReady && !file.empty()) effectFor(file, bus);
}

void playEffect(const std::string& file, Bus bus, float volume, float pan) {
    if (!engineReady || file.empty() || volume <= 0.0f) return;
    Effect& fx = effectFor(file, bus);
    if (fx.copies.empty()) return;
    EffectCopy& copy = *fx.copies[fx.next];
    fx.next = (fx.next + 1) % fx.copies.size();
    ma_sound_stop(&copy.sound);
    ma_sound_seek_to_pcm_frame(&copy.sound, 0);
    ma_sound_set_volume(&copy.sound, std::clamp(volume, 0.0f, 1.0f));
    ma_sound_set_pan(&copy.sound, std::clamp(pan, -1.0f, 1.0f));
    ma_sound_start(&copy.sound);
}
}
