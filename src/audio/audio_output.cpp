#include "audio/audio_output.h"
#include "config/pins.h"
#include <cstdio>

namespace audio {

static struct audio_buffer_pool* audioPool = nullptr;

void audioInit() {
    static audio_format_t audioFormat = {
        .sample_freq = SAMPLE_RATE,
        .format = AUDIO_BUFFER_FORMAT_PCM_S16,
        .channel_count = 2,
    };

    static struct audio_buffer_format producerFormat = {
        .format = &audioFormat,
        .sample_stride = 4, // 2 channels * 2 bytes per sample
    };

    audioPool = audio_new_producer_pool(
        &producerFormat,
        AUDIO_BUFFER_COUNT,
        AUDIO_BUFFER_SAMPLES
    );

    struct audio_i2s_config i2sConfig = {
        .data_pin = I2S_DATA_PIN,
        .clock_pin_base = I2S_BCK_PIN,
        .dma_channel = 0,
        .pio_sm = 0,
    };

    const struct audio_format* outputFormat = audio_i2s_setup(
        &audioFormat,
        &i2sConfig
    );

    if (!outputFormat) {
        printf("ERROR: audio_i2s_setup failed - check I2S wiring (pins 26,27,28)\n");
        panic("audio_i2s_setup failed");
    }
    printf("I2S setup successful\n");

    bool connected = audio_i2s_connect(audioPool);
    if (!connected) {
        printf("ERROR: audio_i2s_connect failed\n");
        panic("audio_i2s_connect failed");
    }
    printf("Audio I2S connected\n");

    audio_i2s_set_enabled(true);
}

struct audio_buffer_pool* getAudioBufferPool() {
    return audioPool;
}

} // namespace audio
