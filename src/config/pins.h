#ifndef PINS_H
#define PINS_H

// I2S output to PCM5102A DAC
#define I2S_DATA_PIN 3
#define I2S_BCK_PIN 4
// LRCK is automatically assigned to BCK_PIN + 1 (GP5)

// MIDI input (UART1)
#define MIDI_UART uart1
#define MIDI_RX_PIN 9
#define MIDI_BAUD_RATE 31250

// Audio sample rate (shared by synth and audio output)
#define SAMPLE_RATE 44100

// Onboard LED (Raspberry Pi Pico, GP25, active-high)
#define LED_PIN PICO_DEFAULT_LED_PIN

#endif // PINS_H
