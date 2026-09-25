#pragma once

#include <cstddef>
#include <cstdint>

namespace doraemon::audio {

bool initialize();
void shutdown();

// Call from the event loop after SDL_PollEvent, not from an SDL event watch:
// Android holds the audio device lock while dispatching foreground events.
void set_suspended(bool suspended);

void set_master_volume(float volume);
float get_master_volume();

void queue_samples(int16_t* samples, size_t sample_count);
size_t get_frames_remaining();
void set_frequency(uint32_t frequency);

} // namespace doraemon::audio
