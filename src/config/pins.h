#ifndef PINS_H
#define PINS_H

// I2S output to PCM5102A DAC
#define I2S_DATA_PIN 26
#define I2S_BCK_PIN 27
// LRCK is automatically assigned to BCK_PIN + 1 (GP28)

// MIDI input (UART1)
#define MIDI_UART uart1
#define MIDI_RX_PIN 5
#define MIDI_BAUD_RATE 31250

// Audio sample rate (shared by synth and audio output)
#define SAMPLE_RATE 44100

// Onboard RGB LED (Tiny2040)
#define LED_RED_PIN 18
#define LED_GREEN_PIN 19
#define LED_BLUE_PIN 20

#endif // PINS_H
