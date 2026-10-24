#pragma once
#include <string>

// Audio output, with three buses that the settings control: Music, Ambient and Sound Effects. Each bus has
// a level from 0 to 100 and can be switched off, which mutes it and keeps the level for when it is back on.
//
// Music is one looped track at a time, read from a BGM folder next to the working directory, the same place
// data.pak is read from.
//
// Nothing here stops the game from running. With no audio output, or a missing, unreadable or corrupt file,
// the sound is simply silent and no error is shown.
namespace audio {

enum class Bus : int { Music = 0, Ambient, Effects, Count };

// Opens the audio output. Returns false if there is none, and the calls below then do nothing.
bool init();

// Stops the music and closes the audio output.
void shutdown();

// Sets a bus's level, 0 to 100, and whether it is heard at all
void setBus(Bus bus, int level, bool on);

// Plays `file`, a name in the BGM folder such as "title.mp3", looped on the Music bus. An empty name stops
// the music. Asking for the track already playing does nothing, so this can be called every frame.
void playMusic(const std::string& file);

// Stops the music at once
void stopMusic();

// The track that is playing, or empty when the music is silent
std::string musicPlaying();

} // namespace audio
