#include "audio.h"

#include <algorithm>
#include <filesystem>
#include "miniaudio.h"

namespace audio {
namespace {

// Tracks are read from this folder, relative to the working directory, like data.pak
const char* const kFolder = "BGM";
constexpr int BUS_COUNT = (int)Bus::Count;

ma_engine engine;
bool engineReady = false;     // the audio output is open
ma_sound_group buses[BUS_COUNT];

ma_sound track;
bool trackReady = false;      // `track` holds a file that loaded
std::string requested;        // the track last asked for, empty when silent

// Stops and frees the current track, if one is loaded
void releaseTrack() {
    if (trackReady) {
        ma_sound_stop(&track);
        ma_sound_uninit(&track);
        trackReady = false;
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
    releaseTrack();
    requested.clear();
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

void playMusic(const std::string& file) {
    if (!engineReady) return;             // no output: nothing to play, and nothing to remember
    if (file == requested) return;
    releaseTrack();
    requested = file;
    if (file.empty()) return;

    // Each request is tried once. A missing file is not looked for again until the track changes.
    const std::filesystem::path path = std::filesystem::path(kFolder) / file;
    const ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
    ma_sound_group* musicBus = &buses[(int)Bus::Music];
#ifdef _WIN32
    const ma_result result = ma_sound_init_from_file_w(&engine, path.c_str(), flags, musicBus, nullptr, &track);
#else
    const ma_result result = ma_sound_init_from_file(&engine, path.string().c_str(), flags, musicBus, nullptr, &track);
#endif
    if (result != MA_SUCCESS) return;     // missing, unreadable or undecodable: silent, no error

    trackReady = true;
    ma_sound_set_looping(&track, MA_TRUE);
    ma_sound_start(&track);
}

void stopMusic() {
    playMusic(std::string());
}

std::string musicPlaying() {
    return trackReady ? requested : std::string();
}

} // namespace audio
