#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/audio_i2s.h"
#include "hardware/gpio.h"
#include "config/pins.h"
#include "synth/supersaw.h"
#include "midi/midi_input.h"
#include "audio/audio_output.h"

static synth::Supersaw supersaw;

void core1_entry() {
    midi::midiInit(MIDI_UART, MIDI_RX_PIN);

    while (true) {
        midi::midiPoll();

        // Check if Core 0 has requested a voice render
        uint32_t cmd = supersaw.core1RenderCmd;
        if (cmd != 0) {
            supersaw.renderCore1Voices(cmd);
            supersaw.core1RenderDone = true;
            supersaw.core1RenderCmd = 0;
        }
    }
}

int main() {
    stdio_init_all();
    printf("Supersaw MIDI Synth starting...\n");
    
    // Initialize onboard RGB LED
    gpio_init(LED_RED_PIN);
    gpio_init(LED_GREEN_PIN);
    gpio_init(LED_BLUE_PIN);
    gpio_set_dir(LED_RED_PIN, GPIO_OUT);
    gpio_set_dir(LED_GREEN_PIN, GPIO_OUT);
    gpio_set_dir(LED_BLUE_PIN, GPIO_OUT);
    gpio_put(LED_RED_PIN, 0);
    gpio_put(LED_GREEN_PIN, 0);
    gpio_put(LED_BLUE_PIN, 0);

    supersaw.init();
    audio::audioInit();

    multicore_launch_core1(core1_entry);
    printf("Core 1 launched (MIDI input + voice rendering).\n");

    struct audio_buffer_pool* pool = audio::getAudioBufferPool();

    while (true) {
        // Process all pending MIDI events from core 1
        midi::MidiEvent event;
        while (midi::midiEventPop(event)) {
            switch (event.type) {
                case midi::MidiEventType::NOTE_ON:
                    supersaw.noteOn(event.param1, event.param2);
                    break;
                case midi::MidiEventType::NOTE_OFF:
                    supersaw.noteOff(event.param1);
                    break;
                case midi::MidiEventType::CC:
                    supersaw.setCC(event.param1, event.param2);
                    break;
            }
        }

        // LED: green while any voice is active
        if (supersaw.anyVoiceActive()) {
            gpio_put(LED_RED_PIN, 0);
            gpio_put(LED_GREEN_PIN, 1);
            gpio_put(LED_BLUE_PIN, 0);
        } else {
            gpio_put(LED_RED_PIN, 0);
            gpio_put(LED_GREEN_PIN, 0);
            gpio_put(LED_BLUE_PIN, 0);
        }

        // Get an audio buffer from the pool (blocks until available)
        struct audio_buffer* buffer = take_audio_buffer(pool, true);
        if (buffer) {
            int16_t* samples = (int16_t*)buffer->buffer->bytes;
            uint32_t numStereoSamples = buffer->max_sample_count;

            supersaw.render(samples, numStereoSamples);

            buffer->sample_count = numStereoSamples;
            give_audio_buffer(pool, buffer);
        }
    }

    return 0;
}
