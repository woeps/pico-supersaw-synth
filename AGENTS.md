# Agent Instructions for Supersaw MIDI Synth

Welcome to the Supersaw MIDI Synth project. This document provides essential information for AI agents contributing to this repository. Read it carefully before proceeding with tasks.

## Project Overview

This project is a MIDI-controlled supersaw synthesizer firmware built for the **Pimoroni Tiny2040** (RP2040). It uses C++ and the official Pico SDK. The architecture relies heavily on custom wavetable synthesis, fixed-point math for performance, and a dual-core processing design.

Key DSP components beyond the core supersaw oscillator:
- **SVFilter** — a Zero-Delay Feedback (ZDF) trapezoidal state-variable filter (LPF/HPF/BPF) that leverages the RP2040 hardware divider for stability.
- **StereoChorus** — a stereo modulated delay effect.
- **I2S audio output** — streams audio via `pico_audio_i2s` to a PCM5102A DAC.

## Hardware Constraints

Always consider the limited hardware constraints of the Pimoroni Tiny2040 (RP2040) when implementing features:
1. **Default CPU frequency**: 125 MHz
2. **RAM**: 264 KB
3. **Flash**: 8 MB QSPI (XiP) — a 2 MB budget variant also exists

Focus on resource efficiency and simplicity. Avoid dynamic memory allocation (`new`/`malloc`) during runtime audio processing. Do not use floating-point math during audio rendering loops as the RP2040 does not have a hardware FPU.

## 1. Build, Lint, and Test Commands

### Prerequisites
- CMake (>= 3.13)
- Ninja build system
- Python 3 (for script execution)
- Raspberry Pi Pico SDK installed and configured in your environment

**Pinned versions** (see `CMakeLists.txt`):
- Pico SDK: **2.1.1**
- ARM toolchain: **14_2_Rel1**
- picotool: **2.1.1**

### Build Commands

This project uses CMake and Ninja to build firmware binaries for the RP2040. 

**Configure the project:**
```bash
cmake -B build -G Ninja
```

**Build the project:**
```bash
ninja -C build
```
This process generates the `.bin`, `.elf`, `.hex`, and `.uf2` firmware files in the `build/` directory.

**Linked Pico SDK libraries** (defined in `CMakeLists.txt`):
- `pico_stdlib` — standard library
- `pico_multicore` — dual-core launch and synchronization
- `pico_audio_i2s` — I2S audio output to PCM5102A DAC
- `hardware_uart` — UART driver for MIDI input
- `hardware_flash` — flash erase/program for preset persistence

### Running a Single Test

**Note:** There is currently no formal unit testing framework (like Google Test) or test suite set up in this repository (no `tests/` directory).

If you are tasked with verifying logic independently from the hardware, you should:
1. Create a minimal standalone C++ executable (e.g., `tests/my_test.cpp`).
2. Add a new `add_executable` and `target_link_libraries` entry in `CMakeLists.txt` (excluding `pico_stdlib` and hardware-specific libraries if running on a host machine).
3. Compile and execute your host test manually.
4. Clean up test executables unless the user specifically asks you to integrate a testing framework.

For on-device testing, flash the generated `build/supersaw_midi_synth.uf2` onto the Pico.

### Linting

There is no dedicated linter script configured. A `compile_commands.json` is generated in `build/` to support `clangd`. Ensure you configure standard C++ linters to respect C++17 standards and Pico SDK headers. Keep code warnings to an absolute minimum.

### Generating Tables

Python scripts are used to precompute lookup tables and wavetables. If you modify core math logic or oscillator parameters, run these scripts to regenerate the C++ source files:
```bash
python3 scripts/generate_tables.py
python3 scripts/generate_filter_table.py
```

## 2. Code Style Guidelines

Adhere to the following conventions when reading or modifying the codebase:

### Languages and Standards
- **C++**: The primary language is C++17.
- **Python**: Used exclusively for code generation (`scripts/`). Use standard PEP-8 conventions.

### Formatting and Indentation
- **Indentation**: 4 spaces per indent level for both C++ and Python. Do not use tabs.
- **Braces**: Open braces `{` on the same line as the statement (`if`, `while`, `class`, `struct`, `namespace`).
- **Line length**: Aim for 80-100 characters max to keep code readable.

### Naming Conventions
- **Namespaces**: `lower_case` (e.g., `synth`, `midi`, `audio`).
- **Classes and Structs**: `PascalCase` (e.g., `Supersaw`, `Voice`, `Envelope`).
- **Enums**: `PascalCase` for enum names (e.g., `EnvStage`, `FilterMode`), `UPPER_SNAKE_CASE` for values (e.g., `NOTE_ON`, `IDLE`). Short uppercase abbreviations are acceptable for filter modes (`LPF`, `HPF`, `BPF`).
- **Functions and Methods**: `camelCase` (e.g., `noteOn`, `renderCore1Voices`).
- **Variables**: `camelCase` (e.g., `nextAge`, `currentMix`).
- **Constants and Macros**: `UPPER_SNAKE_CASE` (e.g., `NUM_OSCILLATORS`, `MAX_VOICES`).

### Types and Math
- **Standard Integer Types**: Prefer fixed-size integer types (`uint32_t`, `int16_t`, `uint8_t`, `size_t`) from `<cstdint>` and `<cstddef>` over standard `int` or `long`.
- **Fixed-Point Arithmetic**: Extensive use of fixed-point math (e.g., Q16.16 or Q8.8) is required to maintain real-time performance. Variables storing fixed-point values should have comments denoting their format (e.g., `// Q16.16`).
- **Pointers and References**: Align pointer and reference markers to the type (e.g., `int16_t* buffer` instead of `int16_t *buffer`).

### Imports and Include Guards
- **C++ Includes**: Use angle brackets for C/C++ standard library headers (`#include <cstdint>`, `#include <cstddef>`). Use double quotes for Pico SDK headers (`#include "pico/stdlib.h"`, `#include "hardware/uart.h"`) and for local project headers relative to the `src` directory (`#include "synth/supersaw.h"`).
- **Include Guards**: Use traditional include guards (`#ifndef SUPERSAW_H \n #define SUPERSAW_H`) rather than `#pragma once`.

### Architecture and State
- **Modular Namespaces**: Keep components modularly isolated inside `synth`, `midi`, `audio`, and `config` namespaces/directories.
  - `src/synth/` — oscillator, envelope, filter, chorus, wavetable data.
  - `src/midi/` — UART MIDI parsing and cross-core event queue.
  - `src/audio/` — I2S audio buffer pool initialization and output.
  - `src/config/` — hardware pin definitions (`pins.h`), MIDI CC assignments (`midi_cc.h`), and flash preset storage (`preset_store.h/.cpp`).
- **Dual-Core Safety**: Core 1 handles MIDI UART **polling/parsing** and renders voices 2–3 into `core1ScratchBuf`. Core 0 **dispatches MIDI events** (`noteOn`/`noteOff`/`setCC`), renders voices 0–1, applies chorus and filter, and writes the final mix to the I2S audio buffer. Use `volatile` modifiers and hardware spinlocks (`spin_lock_t`) for cross-core shared variables.
- **Audio Buffers**: Handle audio via `struct audio_buffer_pool`. Always take and give buffers correctly to prevent deadlocks.
- **Wavetable Band Caching**: High-octave wavetable bands are copied from flash into an SRAM cache (`BandCacheEntry bandCache[MAX_CACHED_BANDS]`) to reduce flash read latency. Access is protected by a hardware spinlock (`cacheLock`) shared between cores. Agents modifying voice allocation or wavetable lookup must understand the `cacheAcquire`/`cacheRelease` protocol in `supersaw.cpp`.
- **Sample Rate**: Defined as `SAMPLE_RATE` (44100) in `src/config/pins.h`. Referenced by both the audio subsystem and DSP code.
- **Preset Persistence**: The BOOTSEL button saves/restores synth parameters to the last 4 KB sector of flash via `preset_store`. Flash writes require `multicore_lockout_start_blocking()` to safely pause Core 1 during erase/program. Saved presets are auto-restored on boot.
- **Precomputation**: Do not calculate sines, complex exponentials, or log functions at runtime. Update `scripts/` to generate look-up tables in `src/synth/` if new complex math is required.

### Error Handling
- Avoid exceptions (`try`, `catch`, `throw`). The project does not enable exceptions to save memory and CPU cycles.
- Use simple boolean returns, error codes, or fallback states for error handling. Let the system fail gracefully and continue processing the next audio buffer if an unexpected state occurs.

## 3. Key Constants

| Constant | Value | Location |
|---|---|---|
| `NUM_OSCILLATORS` | 7 | `src/synth/supersaw.h` |
| `MAX_VOICES` | 4 | `src/synth/supersaw.h` |
| `AUDIO_BUFFER_SAMPLES` | 256 | `src/audio/audio_output.h` |
| `AUDIO_BUFFER_COUNT` | 3 | `src/audio/audio_output.h` |
| `SAMPLE_RATE` | 44100 | `src/config/pins.h` |

## 4. MIDI CC Assignments

All CC numbers are defined in `src/config/midi_cc.h`:

| CC# | Macro | Parameter |
|---|---|---|
| 70 | `CC_FILTER_MODE` | Filter mode (LPF/HPF/BPF) |
| 71 | `CC_FILTER_RESO` | Filter resonance |
| 72 | `CC_RELEASE` | Envelope release |
| 73 | `CC_ATTACK` | Envelope attack |
| 74 | `CC_FILTER_CUTOFF` | Filter cutoff |
| 75 | `CC_DECAY` | Envelope decay |
| 79 | `CC_SUSTAIN` | Envelope sustain |
| 91 | `CC_CHORUS_DEPTH` | Chorus depth |
| 92 | `CC_CHORUS_RATE` | Chorus rate |
| 93 | `CC_SPREAD` | Stereo spread |
| 94 | `CC_DETUNE` | Oscillator detune |
| 95 | `CC_MIX` | Supersaw mix |

## 5. Documentation Rules

After a successful modification of the code, you **must** update the documentation in the `docs/` folder.
Relevant files include:
- `docs/architecture.md`
- `docs/design-decisions.md`
- `docs/supersaw.md`
- `docs/wiring.md`

The documentation site uses **Docsify**. The sidebar navigation is defined in `docs/_sidebar.md`, and the entry point is `docs/index.html`. When adding new pages, update the sidebar accordingly.

Ensure that any changes to MIDI CC mappings, hardware pinouts, or synthesis algorithms are accurately reflected in these Markdown files.