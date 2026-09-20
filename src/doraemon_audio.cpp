#include "doraemon_audio.hpp"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <vector>

namespace doraemon::audio {
namespace {

constexpr uint32_t input_channels = 2;
constexpr float base_output_gain = 0.5f;
constexpr size_t max_input_samples = 0x20000;

std::mutex audio_mutex;
SDL_AudioDeviceID audio_device = 0;
SDL_AudioStream* audio_stream = nullptr;
uint32_t input_sample_rate = 48000;
uint32_t output_sample_rate = 48000;
uint32_t output_channels = 2;
uint64_t maximum_submitted_frames = 0;
std::atomic<float> master_volume{1.0f};

std::vector<float> input_buffer;
std::vector<Uint8> output_buffer;

bool rebuild_stream_locked() {
    if (audio_stream != nullptr) {
        SDL_FreeAudioStream(audio_stream);
        audio_stream = nullptr;
    }

    if (audio_device == 0 || input_sample_rate == 0 || output_sample_rate == 0) {
        return false;
    }

    audio_stream = SDL_NewAudioStream(
        AUDIO_F32SYS,
        input_channels,
        static_cast<int>(input_sample_rate),
        AUDIO_F32SYS,
        static_cast<Uint8>(output_channels),
        static_cast<int>(output_sample_rate)
    );

    if (audio_stream == nullptr) {
        std::fprintf(stderr, "SDL_NewAudioStream failed: %s\n", SDL_GetError());
        return false;
    }

    return true;
}

bool drain_stream_locked() {
    if (audio_stream == nullptr || audio_device == 0) {
        return false;
    }

    for (;;) {
        const int available = SDL_AudioStreamAvailable(audio_stream);
        if (available < 0) {
            std::fprintf(stderr, "SDL_AudioStreamAvailable failed: %s\n", SDL_GetError());
            return false;
        }
        if (available == 0) {
            return true;
        }

        output_buffer.resize(static_cast<size_t>(available));
        const int received = SDL_AudioStreamGet(audio_stream, output_buffer.data(), available);
        if (received < 0) {
            std::fprintf(stderr, "SDL_AudioStreamGet failed: %s\n", SDL_GetError());
            return false;
        }
        if (received == 0) {
            return true;
        }
        if (SDL_QueueAudio(audio_device, output_buffer.data(), static_cast<Uint32>(received)) != 0) {
            std::fprintf(stderr, "SDL_QueueAudio failed: %s\n", SDL_GetError());
            return false;
        }
    }
}

} // namespace

void set_master_volume(float volume) {
    master_volume.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
}

float get_master_volume() {
    return master_volume.load(std::memory_order_relaxed);
}

bool initialize() {
    std::scoped_lock lock(audio_mutex);

    if (audio_device != 0) {
        return true;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL audio initialization failed: %s\n", SDL_GetError());
        return false;
    }

    SDL_AudioSpec desired{};
    desired.freq = static_cast<int>(output_sample_rate);
    desired.format = AUDIO_F32SYS;
    desired.channels = static_cast<Uint8>(output_channels);
    desired.samples = 0x100;
    desired.callback = nullptr;

    SDL_AudioSpec obtained{};
    audio_device = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (audio_device == 0) {
        std::fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    output_sample_rate = static_cast<uint32_t>(obtained.freq);
    output_channels = static_cast<uint32_t>(obtained.channels);

    if (obtained.format != AUDIO_F32SYS || output_channels != 2) {
        std::fprintf(
            stderr,
            "Unsupported SDL audio format: format=0x%04X channels=%u\n",
            static_cast<unsigned>(obtained.format),
            output_channels
        );
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    if (!rebuild_stream_locked()) {
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    SDL_PauseAudioDevice(audio_device, 0);
    std::printf(
        "Audio initialized: driver=%s, output=%u Hz, stereo float32, device buffer=%u frames (%llums)\n",
        SDL_GetCurrentAudioDriver() != nullptr ? SDL_GetCurrentAudioDriver() : "unknown",
        output_sample_rate,
        static_cast<unsigned>(obtained.samples),
        static_cast<unsigned long long>(
            static_cast<uint64_t>(obtained.samples) * 1000 / output_sample_rate
        )
    );
    return true;
}

void shutdown() {
    std::scoped_lock lock(audio_mutex);

    if (audio_stream != nullptr) {
        SDL_FreeAudioStream(audio_stream);
        audio_stream = nullptr;
    }

    if (audio_device != 0) {
        SDL_ClearQueuedAudio(audio_device);
        SDL_CloseAudioDevice(audio_device);
        audio_device = 0;
    }

    input_buffer.clear();
    output_buffer.clear();
    maximum_submitted_frames = 0;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void set_frequency(uint32_t frequency) {
    if (frequency == 0) {
        return;
    }

    std::scoped_lock lock(audio_mutex);
    if (frequency == input_sample_rate && audio_stream != nullptr) {
        return;
    }

    input_sample_rate = frequency;
    maximum_submitted_frames = 0;
    if (audio_device != 0) {
        SDL_ClearQueuedAudio(audio_device);
    }

    if (rebuild_stream_locked()) {
        std::printf("Audio input frequency: %u Hz\n", input_sample_rate);
        std::fflush(stdout);
    }
}

void queue_samples(int16_t* samples, size_t sample_count) {
    if (samples == nullptr || sample_count < input_channels) {
        return;
    }

    sample_count -= sample_count % input_channels;
    if (sample_count > max_input_samples) {
        std::fprintf(
            stderr,
            "Rejected invalid N64 audio buffer: %zu samples (maximum %zu)\n",
            sample_count,
            max_input_samples
        );
        return;
    }

    std::scoped_lock lock(audio_mutex);
    if (audio_device == 0 || audio_stream == nullptr) {
        return;
    }

    maximum_submitted_frames = std::max(
        maximum_submitted_frames,
        static_cast<uint64_t>(sample_count / input_channels)
    );

    const float output_gain =
        base_output_gain * master_volume.load(std::memory_order_relaxed);
    input_buffer.resize(sample_count);
    for (size_t i = 0; i < sample_count; i += input_channels) {
        // N64Recomp's word-addressed RDRAM representation swaps the two
        // 16-bit samples in every stereo frame.
        const int32_t left = samples[i + 1];
        const int32_t right = samples[i + 0];
        input_buffer[i + 0] = static_cast<float>(left) * (output_gain / 32768.0f);
        input_buffer[i + 1] = static_cast<float>(right) * (output_gain / 32768.0f);
    }

    if (SDL_AudioStreamPut(
            audio_stream,
            input_buffer.data(),
            static_cast<int>(sample_count * sizeof(float))
        ) != 0) {
        std::fprintf(stderr, "SDL_AudioStreamPut failed: %s\n", SDL_GetError());
        return;
    }

    if (!drain_stream_locked()) {
        return;
    }

}

size_t get_frames_remaining() {
    std::scoped_lock lock(audio_mutex);
    if (audio_device == 0 || output_sample_rate == 0) {
        return 0;
    }

    uint64_t output_frames =
        SDL_GetQueuedAudioSize(audio_device) / (output_channels * sizeof(float));
    if (audio_stream != nullptr) {
        const int stream_bytes = SDL_AudioStreamAvailable(audio_stream);
        if (stream_bytes > 0) {
            output_frames += static_cast<uint64_t>(stream_bytes) /
                (output_channels * sizeof(float));
        }
    }
    uint64_t input_frames = output_frames * input_sample_rate / output_sample_rate;

    // SDL keeps some audio queued ahead of the N64's current AI buffer. Do not
    // expose that host-side safety margin to the game: Doraemon subtracts the
    // reported AI length when choosing the next frame's sample count, and
    // counting the margin would make that sample count negative.
    // Keep one VI hidden as a host-side safety margin. The runtime hides
    // another half VI. WSLg/PulseAudio needs this margin to avoid underruns.
    const uint64_t host_buffer_offset = input_sample_rate / 60;
    if (input_frames > host_buffer_offset) {
        input_frames -= host_buffer_offset;
    }
    else {
        input_frames = 0;
    }

    // AI_LEN exposes only the DMA that is currently playing. The SDL queue can
    // also contain later DMA buffers, so its total must never be reported as
    // the current N64 buffer length. No current DMA can be larger than the
    // largest valid buffer the game has submitted.
    if (maximum_submitted_frames != 0 && input_frames > maximum_submitted_frames) {
        input_frames = maximum_submitted_frames;
    }

    return static_cast<size_t>(input_frames);
}

} // namespace doraemon::audio
