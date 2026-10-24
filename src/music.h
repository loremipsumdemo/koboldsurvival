#pragma once
#include <string>

// Background music. A track is an audio file (MP3, or any other format miniaudio decodes) in a BGM
// folder next to the working directory, the same place data.pak is read from. A track plays looped,
// and one track plays at a time.
//
// Nothing here stops the game from running. With no audio output, or a missing, unreadable or corrupt
// file, the music is simply silent and no error is shown.
namespace music {

// Opens the audio output. Returns false if there is none, and the calls below then do nothing.
// Until this succeeds, play() does nothing, so it is safe to call it earlier than init().
bool init();

// Stops the music and closes the audio output.
void shutdown();

// Makes `file`, a name in the BGM folder such as "title.mp3", the playing track, looping. An empty
// name stops the music. Asking for the track that is already requested does nothing, so this can be
// called every frame.
void play(const std::string& file);

// Stops the music at once.
inline void stop() { play(std::string()); }

// The track requested by the last play() call when it is audible, or empty when the music is silent.
std::string nowPlaying();

} // namespace music
