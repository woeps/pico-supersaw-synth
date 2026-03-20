# SuperSaw MIDI Synth

Simple MIDI-controlled supersaw synthesizer for Raspberry Pi Pico.

## Hardware

- Raspberry Pi Pico (RP2040) microcontroller
- PCM5102A DAC
- Serial MIDI-Input (UART)
- Audio Output (TRS)

## Technology

- Written in C++ with pico-sdk
- Focus on resource efficiency and simplicity

## Synthesizer

The only supported waveform is a supersaw, targeting up to 4 voices.

### Parameters

- **Envelope**: attack, decay, sustain, release
- **Detune**
- **Spread**

## Documentation

- [Architecture](architecture.md)
- [Building](building.md)
- [Design Decisions](design-decisions.md)
- [SuperSaw Algorithm](supersaw.md)
- [Wiring](wiring.md)
