#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include "pico/audio_i2s.h"
#include "config/pins.h"

namespace audio {
#define AUDIO_BUFFER_SAMPLES 256
#define AUDIO_BUFFER_COUNT 3

void audioInit();
struct audio_buffer_pool* getAudioBufferPool();

} // namespace audio

#endif // AUDIO_OUTPUT_H
