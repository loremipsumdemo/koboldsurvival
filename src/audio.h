#pragma once
#include <string>

namespace audio {
enum class Bus : int { Music = 0, Ambient, Effects, Interface, Count };

bool init();
void shutdown();

void setBus(Bus bus, int level, bool on);
void setGlobal(int level, bool on);

void playTrack(Bus bus, const std::string& file);
// Opens the bus's track without starting it, so playTrack with the same file starts it at once
void preloadTrack(Bus bus, const std::string& file);
void stopTracks();

void preloadEffect(const std::string& file, Bus bus);
void playEffect(const std::string& file, Bus bus = Bus::Effects, float volume = 1.0f, float pan = 0.0f);
}
