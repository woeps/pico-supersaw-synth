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
    printf("Core 1 launched (MIDI input).\n");

    struct audio_buffer_pool* pool = audio::getAudioBufferPool();

    while (true) {
        // Process all pending MIDI events from core 1
        midi::MidiEvent event;
        while (midi::midiEventPop(event)) {
            if (event.gate) {
                supersaw.noteOn(event.note, 127);
                gpio_put(LED_RED_PIN, 0);   // Turn LED green
                gpio_put(LED_GREEN_PIN, 1);  // Turn LED green
                gpio_put(LED_BLUE_PIN, 0);   // Turn LED green
            } else {
                supersaw.noteOff();
                gpio_put(LED_RED_PIN, 0);   // Turn LED off
                gpio_put(LED_GREEN_PIN, 0);  // Turn LED off
                gpio_put(LED_BLUE_PIN, 0);   // Turn LED off
            }
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
