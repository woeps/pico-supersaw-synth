# Architecture

## Overview

The supersaw-midi-synth is a 4-voice polyphonic MIDI-controlled synthesizer running on the RP2040 (Raspberry Pi Pico). It receives MIDI note on/off and control change messages via UART and outputs stereo audio through a PCM5102A I2S DAC.

## Signal Flow

```
MIDI IN (UART1 RX) → MIDI Parser → Multicore FIFO → Voice Allocator → 4× Supersaw Voices → Stereo Mix → Stereo Chorus → I2S Audio Out → PCM5102A DAC
```

## Dual-Core Layout

### Core 0 — Audio Rendering & Event Processing
- Initializes all peripherals (stdio, I2S audio, supersaw)
- Launches core 1
- Runs the main loop:
  1. Drains pending MIDI events from the multicore FIFO
  2. Dispatches note on/off to the voice allocator and CC messages to parameter update
  3. Takes an audio buffer from the I2S pool (blocks until available)
  4. Renders all active voices into the stereo buffer
  5. Submits the buffer for DMA playback

### Core 1 — MIDI Input
- Initializes UART1 for MIDI reception
- Continuously polls for incoming MIDI bytes
- Parses note on/off and control change messages via a state machine
- Pushes typed `MidiEvent` structs through the multicore FIFO

## Inter-Core Communication

Communication uses the RP2040's hardware multicore FIFO (single-producer, single-consumer). Each MIDI event is packed into a 32-bit word:

```
Bits [17:16]  event type  (NOTE_ON / NOTE_OFF / CC)
Bits [14:8]   param1      (note number or CC number)
Bits [6:0]    param2      (velocity or CC value)
```

Core 1 pushes events, core 0 pops and dispatches them. The FIFO is lock-free and interrupt-safe.

## Module Responsibilities

| Module | Files | Purpose |
|--------|-------|---------|
| **synth** | `src/synth/supersaw.h/.cpp` | 4-voice polyphonic supersaw with ADSR envelope |
| **synth** | `src/synth/chorus.h/.cpp` | Stereo chorus effect (post-mix) |
| **midi** | `src/midi/midi_input.h/.cpp` | UART MIDI parsing, event packing |
| **audio** | `src/audio/audio_output.h/.cpp` | I2S output setup via pico_audio_i2s |
| **config** | `src/config/pins.h` | Pin definitions |
| **config** | `src/config/midi_cc.h` | MIDI CC number assignments |
| **main** | `src/main.cpp` | Entry point, dual-core orchestration |
