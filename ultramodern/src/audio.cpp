#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include <cassert>
#include <cstdio>

static uint32_t sample_rate = 48000;

static ultramodern::audio_callbacks_t audio_callbacks;

void ultramodern::set_audio_callbacks(const ultramodern::audio_callbacks_t& callbacks) {
    audio_callbacks = callbacks;
}

void ultramodern::init_audio() {
    // Pick an initial dummy sample rate; this will be set by the game later to the true sample rate.
    set_audio_frequency(48000);
}

void ultramodern::set_audio_frequency(uint32_t freq) {
    if (audio_callbacks.set_frequency) {
        audio_callbacks.set_frequency(freq);
    }
    sample_rate = freq;
}

void ultramodern::queue_audio_buffer(RDRAM_ARG PTR(int16_t) audio_data_, uint32_t byte_count) {
    // Ensure that the byte count is an integer multiple of samples.
    assert((byte_count & 1) == 0);

    // A game may give its buffer's uncached address (KSEG1, from 0xA0000000), which TO_PTR would put 512 MB past
    // RDRAM: it's read through the cached one (KSEG0) for the same memory. A buffer that doesn't lie in RDRAM's 8 MB
    // isn't played, as reading it would read past RDRAM (into another allocation on a page, which has no guard region).
    const uint32_t rdram_address = uint32_t(audio_data_) & 0x1FFFFFFF;
    if ((uint32_t(audio_data_) & 0xC0000000) != 0x80000000 || rdram_address > 0x800000 || byte_count > 0x800000 - rdram_address) {
        static bool reported = false;
        if (!reported) {
            reported = true;
            fprintf(stderr, "ultramodern: a sound buffer outside RDRAM (0x%08X, %u bytes) isn't played\n", uint32_t(audio_data_), byte_count);
        }
        return;
    }
    audio_data_ = PTR(int16_t)(rdram_address | 0x80000000);

    // Calculate the number of samples from the number of bytes.
    uint32_t sample_count = byte_count / sizeof(int16_t);

    // Queue the swapped audio data.
    if (sample_count > 0 && audio_callbacks.queue_samples) {
        audio_callbacks.queue_samples(TO_PTR(int16_t, audio_data_), sample_count);
    }
}

// For SDL2
//uint32_t buffer_offset_frames = 1;
// For Godot
float buffer_offset_frames = 0.5f;

// If there's ever any audio popping, check here first. Some games are very sensitive to
// the remaining sample count and reporting a number that's too high here can lead to issues.
// Reporting a number that's too low can lead to audio lag in some games.
uint32_t ultramodern::get_remaining_audio_bytes() {
    // Get the number of remaining buffered audio bytes.
    uint32_t buffered_byte_count;
    if (audio_callbacks.get_frames_remaining != nullptr) {
        buffered_byte_count = audio_callbacks.get_frames_remaining() * 2 * sizeof(int16_t);
    }
    else {
        buffered_byte_count = 100;
    }
    // Adjust the reported count to be some number of refreshes in the future, which helps ensure that
    // there are enough samples even if the audio thread experiences a small amount of lag. This prevents
    // audio popping on games that use the buffered audio byte count to determine how many samples
    // to generate.
    uint32_t samples_per_vi = (sample_rate / 60);
    if (buffered_byte_count > static_cast<uint32_t>(buffer_offset_frames * sizeof(int16_t) * samples_per_vi)) {
        buffered_byte_count -= static_cast<uint32_t>(buffer_offset_frames * sizeof(int16_t) * samples_per_vi);
    }
    else {
        buffered_byte_count = 0;
    }
    return buffered_byte_count;
}
