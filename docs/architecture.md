# Architecture

## Overview

The supersaw-midi-synth is a single-voice MIDI-controlled synthesizer running on the RP2040 (Raspberry Pi Pico). It receives MIDI note on/off messages via UART and outputs audio through a PCM5102A I2S DAC.

## Signal Flow

```
MIDI IN (UART1 RX) → MIDI Parser → Shared State → Supersaw Oscillator → I2S Audio Out → PCM5102A DAC
```

## Dual-Core Layout

### Core 0 — Audio Rendering
- Initializes all peripherals (stdio, I2S audio, supersaw)
- Launches core 1
- Runs the main audio render loop:
  1. Checks shared MIDI state for note events
  2. Takes an audio buffer from the I2S pool (blocks until available)
  3. Renders supersaw samples into the buffer
  4. Submits the buffer for DMA playback

### Core 1 — MIDI Input
- Initializes UART1 for MIDI reception
- Continuously polls for incoming MIDI bytes
- Parses note on/off messages via a state machine
- Updates shared volatile state (gate, note, event flag)

## Inter-Core Communication

Shared state is simple and lock-free:
- `volatile bool gMidiGate` — true while a note is held
- `volatile uint8_t gMidiNote` — current MIDI note number
- `volatile bool gMidiEvent` — set by core 1, cleared by core 0

This works because only core 1 writes and only core 0 reads (single-producer, single-consumer).

## Module Responsibilities

| Module | Files | Purpose |
|--------|-------|---------|
| **synth** | `src/synth/supersaw.h/.cpp` | 7-oscillator supersaw sound generation |
| **midi** | `src/midi/midi_input.h/.cpp` | UART MIDI parsing, shared state |
| **audio** | `src/audio/audio_output.h/.cpp` | I2S output setup via pico_audio_i2s |
| **config** | `src/config/pins.h` | Pin definitions |
| **main** | `src/main.cpp` | Entry point, dual-core orchestration |
