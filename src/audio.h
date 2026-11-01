#pragma once
#include <string>

// Game audio, through miniaudio. There are three buses, each with a level from 0 to 100 and an on/off switch that
// keeps the level. The music and ambient buses each loop one track; the effects bus plays one-shot sounds.
// Tracks are read from the BGM folder and sound effects from the SFX folder: loose files next to the executable,
// not packed into data.pak. A file that is missing or cannot be decoded is silent, with no error.
namespace audio {

// Interface: the menus' and buttons' sounds, on their own slider
enum class Bus : int { Music = 0, Ambient, Effects, Interface, Count };

// Opens the audio output. Without one, everything below is a quiet no-op. Returns whether it opened.
bool init();
void shutdown();

// Sets a bus's level, 0 to 100, and whether it is heard
void setBus(Bus bus, int level, bool on);
// Sets the global level, 0 to 100, and whether anything is heard. It scales every bus.
void setGlobal(int level, bool on);

// Loops a track on the music or the ambient bus. Asking for the track already playing does not restart it, and
// "" stops the bus. The other buses hold no tracks, so they are ignored.
void playTrack(Bus bus, const std::string& file);
// Stops the music and the ambient tracks
void stopTracks();
// The track a bus is playing, or "" when none is
std::string trackPlaying(Bus bus);

// Plays a sound effect once on a bus (the effects bus unless told otherwise). Each call starts it from the beginning
// on the next of a few copies in turn, so the same effect can overlap itself a few times. A copy still playing is
// restarted.
void playEffect(const std::string& file, Bus bus = Bus::Effects);

} // namespace audio
