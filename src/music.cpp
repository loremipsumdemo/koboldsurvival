#include "music.h"

#include <filesystem>
#include "miniaudio.h"

namespace music {
namespace {

// Tracks are read from this folder, relative to the working directory, like data.pak
const char* const kFolder = "BGM";

ma_engine engine;
bool engineReady = false;     // the audio output is open
ma_sound sound;
bool soundReady = false;      // `sound` holds a track that loaded
std::string requested;        // the track last asked for, empty when silent

// Stops and frees the current track, if one is loaded
void releaseTrack() {
    if (soundReady) {
        ma_sound_stop(&sound);
        ma_sound_uninit(&sound);
        soundReady = false;
    }
}

} // namespace

bool init() {
    if (engineReady) return true;
    const ma_engine_config config = ma_engine_config_init();
    if (ma_engine_init(&config, &engine) != MA_SUCCESS) return false;
    engineReady = true;
    return true;
}

void shutdown() {
    releaseTrack();
    requested.clear();
    if (engineReady) {
        ma_engine_uninit(&engine);
        engineReady = false;
    }
}

void play(const std::string& file) {
    if (!engineReady) return;             // no output: nothing to play, and nothing to remember
    if (file == requested) return;
    releaseTrack();
    requested = file;
    if (file.empty()) return;

    // Each request is tried once. A missing file is not looked for again until the track changes.
    const std::filesystem::path path = std::filesystem::path(kFolder) / file;
    const ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
#ifdef _WIN32
    const ma_result result = ma_sound_init_from_file_w(&engine, path.c_str(), flags, nullptr, nullptr, &sound);
#else
    const ma_result result = ma_sound_init_from_file(&engine, path.string().c_str(), flags, nullptr, nullptr, &sound);
#endif
    if (result != MA_SUCCESS) return;   // missing, unreadable or undecodable: silent, no error

    soundReady = true;
    ma_sound_set_looping(&sound, MA_TRUE);
    ma_sound_start(&sound);
}

std::string nowPlaying() {
    return soundReady ? requested : std::string();
}

} // namespace music
