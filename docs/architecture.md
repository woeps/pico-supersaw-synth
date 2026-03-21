# Architecture

## Overview

The supersaw-midi-synth is a 4-voice polyphonic MIDI-controlled synthesizer running on the RP2040 (Raspberry Pi Pico). It receives MIDI note on/off and control change messages via UART and outputs stereo audio through a PCM5102A I2S DAC.

## Signal Flow

```
MIDI IN (UART1 RX) → MIDI Parser → Multicore FIFO → Voice Allocator
                                                         ↓
                                          ┌──────────────┴──────────────┐
                                     Core 0: Voices 0–1          Core 1: Voices 2–3
                                          └──────────────┬──────────────┘
                                                         ↓
                                                   Merge & ÷4 → SVF Filter → Stereo Chorus → I2S Audio Out → PCM5102A DAC
```

## Dual-Core Layout

### Core 0 — Event Processing, Voices 0–1, Merge & Output
- Initializes all peripherals (stdio, I2S audio, supersaw)
- Launches core 1
- Runs the main loop:
  1. Drains pending MIDI events from the multicore FIFO
  2. Dispatches note on/off to the voice allocator and CC messages to parameter update
  3. Takes an audio buffer from the I2S pool (blocks until available)
  4. Signals Core 1 to render voices 2–3 via a shared volatile flag
  5. Renders voices 0–1 into the output buffer (with per-sample parameter smoothing)
  6. Waits for Core 1 to finish
  7. Merges Core 1's scratch buffer, applies ÷4 normalization, clamp, and stereo chorus
  8. Submits the buffer for DMA playback

### Core 1 — MIDI Input & Voices 2–3
- Initializes UART1 for MIDI reception
- Continuously polls for incoming MIDI bytes
- Parses note on/off and control change messages via a state machine
- Pushes typed `MidiEvent` structs through the multicore FIFO
- Between MIDI polls, checks for a render command from Core 0
- When signaled, renders voices 2–3 into a shared scratch buffer and signals completion

## Inter-Core Communication

### MIDI Events

MIDI events use the RP2040's hardware multicore FIFO (single-producer, single-consumer). Each event is packed into a 32-bit word:

```
Bits [17:16]  event type  (NOTE_ON / NOTE_OFF / CC)
Bits [14:8]   param1      (note number or CC number)
Bits [6:0]    param2      (velocity or CC value)
```

Core 1 pushes events, core 0 pops and dispatches them. The FIFO is lock-free and interrupt-safe.

### Voice Render Synchronization

Voice rendering is split equally across both cores using volatile shared variables:

- `core1RenderCmd` — Core 0 writes the buffer sample count to trigger Core 1 rendering
- `core1RenderDone` — Core 1 sets this flag when its voices are rendered
- `core1ScratchBuf` — int32_t buffer where Core 1 writes its partial stereo mix

Core 1 polls this flag alongside MIDI, ensuring sub-microsecond response to render requests. No FIFO or mutex is needed — volatile is sufficient on Cortex-M0+ (no data cache, no out-of-order execution).

The shared `bandCache` (wavetable SRAM cache) is protected by an RP2040 hardware spinlock, since both cores may call `cacheRelease()` when a voice envelope reaches IDLE during parallel rendering.

## Module Responsibilities

| Module | Files | Purpose |
|--------|-------|---------|
| **synth** | `src/synth/supersaw.h/.cpp` | 4-voice polyphonic supersaw with ADSR envelope |
| **synth** | `src/synth/filter.h/.cpp` | Resonant SVF filter (LPF/HPF/BPF, post-mix) |
| **synth** | `src/synth/chorus.h/.cpp` | Stereo chorus effect (post-filter) |
| **midi** | `src/midi/midi_input.h/.cpp` | UART MIDI parsing, event packing |
| **audio** | `src/audio/audio_output.h/.cpp` | I2S output setup via pico_audio_i2s |
| **config** | `src/config/pins.h` | Pin definitions |
| **config** | `src/config/midi_cc.h` | MIDI CC number assignments |
| **main** | `src/main.cpp` | Entry point, dual-core orchestration |
