#include "audio/audio_output.h"
#include "config/pins.h"
#include "hardware/resets.h"
#include <cstdio>

namespace audio {

static struct audio_buffer_pool* audioPool = nullptr;

void audioInit() {
    // Hard-reset PIO0 and DMA to clear any running state from SWD soft-reset
    reset_block(RESETS_RESET_PIO0_BITS | RESETS_RESET_PIO1_BITS | RESETS_RESET_DMA_BITS);
    unreset_block_wait(RESETS_RESET_PIO0_BITS | RESETS_RESET_PIO1_BITS | RESETS_RESET_DMA_BITS);

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
        panic("audio_i2s_setup failed");
    }

    bool connected = audio_i2s_connect(audioPool);
    if (!connected) {
        panic("audio_i2s_connect failed");
    }

    audio_i2s_set_enabled(true);
}

struct audio_buffer_pool* getAudioBufferPool() {
    return audioPool;
}

} // namespace audio
