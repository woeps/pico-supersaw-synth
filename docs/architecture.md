# Architecture

## Overview

The supersaw-midi-synth is a 4-voice polyphonic MIDI-controlled synthesizer running on the RP2040 (Raspberry Pi Pico). It receives MIDI note on/off and control change messages via UART and outputs stereo audio through a PCM5102A I2S DAC.

## Signal Flow

```
MIDI IN (UART1 RX) → MIDI Parser → Software Ring Buffer → Voice Allocator
                                                         ↓
                                          ┌──────────────┴──────────────┐
                                     Core 0: Voices 0–1          Core 1: Voices 2–3
                                          └──────────────┬──────────────┘
                                                         ↓
                                                   Merge & ÷2 → ZDF Filter (reso-compensated) → Stereo Chorus (crossfade) → I2S Audio Out → PCM5102A DAC
```

## Dual-Core Layout

### Core 0 — Event Processing, Voices 0–1, Merge & Output
- Initializes all peripherals (stdio, I2S audio, supersaw)
- Launches core 1
- Runs the main loop:
  1. Drains pending MIDI events from the software ring buffer
  2. Dispatches note on/off to the voice allocator and CC messages to parameter update
  3. Takes an audio buffer from the I2S pool (blocks until available)
  4. Signals Core 1 to render voices 2–3 via a shared volatile flag
  5. Renders voices 0–1 into an int32_t scratch buffer (with per-sample parameter smoothing)
  6. Waits for Core 1 to finish
  7. Merges both int32_t scratch buffers, applies ÷2 normalization, clamp, filter, and stereo chorus
  8. Submits the buffer for DMA playback

### Core 1 — MIDI Input & Voices 2–3
- Initializes UART1 for MIDI reception
- Continuously polls for incoming MIDI bytes
- Parses note on/off and control change messages via a state machine
- Pushes typed `MidiEvent` structs through the software ring buffer
- Between MIDI polls, checks for a render command from Core 0
- When signaled, renders voices 2–3 into a shared scratch buffer and signals completion

## Inter-Core Communication

### MIDI Events

MIDI events use a lock-free, single-producer, single-consumer software ring buffer (256 elements deep). Each event is packed into a 32-bit word:

```
Bits [17:16]  event type  (NOTE_ON / NOTE_OFF / CC)
Bits [14:8]   param1      (note number or CC number)
Bits [6:0]    param2      (velocity or CC value)
```

Core 1 pushes events, core 0 pops and dispatches them. The ring buffer is lock-free and cross-core safe using memory barriers. If the queue overflows, Core 1 sets a boolean panic flag. When Core 0 reads this flag, it clears the queue and issues a `PANIC` event which immediately gates off all active voices to prevent hanging notes.

### Voice Render Synchronization

Voice rendering is split equally across both cores using volatile shared variables:

- `core1RenderCmd` — Core 0 writes the buffer sample count to trigger Core 1 rendering
- `core1RenderDone` — Core 1 sets this flag when its voices are rendered
- `core0ScratchBuf` — int32_t buffer where Core 0 writes its partial stereo mix
- `core1ScratchBuf` — int32_t buffer where Core 1 writes its partial stereo mix

Core 1 polls this flag alongside MIDI, ensuring sub-microsecond response to render requests. No FIFO or mutex is needed — volatile is sufficient on Cortex-M0+ (no data cache, no out-of-order execution).

The shared `bandCache` (wavetable SRAM cache) and **voice struct mutations** are protected by the same RP2040 hardware spinlock (`cacheLock`). Both cores may call `cacheRelease()` when a voice envelope reaches IDLE during parallel rendering. Core 0's `noteOn()` and `noteOff()` also hold this spinlock while mutating voice fields (`note`, `velocity`, `active`, `age`, `env`) to prevent Core 1 from reading a partially-written voice struct.

## Module Responsibilities

| Module | Files | Purpose |
|--------|-------|---------|
| **synth** | `src/synth/supersaw.h/.cpp` | 4-voice polyphonic supersaw with ADSR envelope |
| **synth** | `src/synth/filter.h/.cpp` | Resonant ZDF state-variable filter (LPF/BPF/HPF via CC 70, post-mix, Q28 internals) |
| **synth** | `src/synth/chorus.h/.cpp` | Stereo chorus effect (post-filter) |
| **midi** | `src/midi/midi_input.h/.cpp` | UART MIDI parsing, event packing |
| **audio** | `src/audio/audio_output.h/.cpp` | I2S output setup via pico_audio_i2s |
| **config** | `src/config/pins.h` | Pin definitions |
| **config** | `src/config/midi_cc.h` | MIDI CC number assignments |
| **config** | `src/config/preset_store.h/.cpp` | Flash preset save/restore |
| **main** | `src/main.cpp` | Entry point, dual-core orchestration, BOOTSEL button handling |

## Preset Persistence (BOOTSEL Button)

The BOOTSEL button on the Tiny2040 doubles as a user-facing preset control:

| Action | Effect | LED Feedback |
|---|---|---|
| Hold < 3 s, then release | Restore saved preset | Blue flash (500 ms) |
| Hold ≥ 3 s | Entering save mode | Red blink (250 ms period) |
| Hold ≥ 5 s (3 + 2) | Save current parameters | Green flash (500 ms) |
| Power-on / reboot | Auto-restore if valid preset exists | — |

All 12 CC-controllable parameters are persisted as raw `uint8_t` values in a 256-byte flash page at the last 4 KB sector. The `preset_store` module handles flash erase/program, while `main.cpp` manages the button state machine and LED feedback.

Flash writes require pausing Core 1 via `multicore_lockout_start_blocking()` because the RP2040 executes code from flash (XiP). Core 1 calls `multicore_lockout_victim_init()` at startup to enable safe lockout. The brief audio dropout during save (~few ms) is acceptable for a user-initiated action.
