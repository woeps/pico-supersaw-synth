# SuperSaw MIDI Synth

Simple MIDI-controlled supersaw synthesizer for Raspberry Pi Pico.

## Hardware

- Pimoroni Tiny2040 (RP2040) microcontroller
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
- **Detune** (non-linear JP-8000 curve)
- **Mix** (center vs. side oscillator balance)
- **Spread**
- **Chorus** (depth and rate)
- **Filter** (cutoff, resonance, mode: LPF/BPF/HPF)

## Documentation

- [Architecture](architecture.md)
- [Building](building.md)
- [Design Decisions](design-decisions.md)
- [SuperSaw Algorithm](supersaw.md)
- [Wiring](wiring.md)
