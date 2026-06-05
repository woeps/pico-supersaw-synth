#include "audio/audio_output.h"
#include "config/pins.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
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

    // Find free DMA channel and PIO SM, then unclaim so pico_audio_i2s can
    // claim them internally (the library calls dma_channel_claim/pio_sm_claim).
    int dma_ch = dma_claim_unused_channel(true);
    dma_channel_unclaim(dma_ch);
    int sm = pio_claim_unused_sm(pio0, true);
    pio_sm_unclaim(pio0, sm);

    struct audio_i2s_config i2sConfig = {
        .data_pin = I2S_DATA_PIN,
        .clock_pin_base = I2S_BCK_PIN,
        .dma_channel = static_cast<uint8_t>(dma_ch),
        .pio_sm = static_cast<uint8_t>(sm),
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
