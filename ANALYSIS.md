# Codebase Analysis: Supersaw MIDI Synth

## 1. Bugs (Sorted by Criticality)

**[FIXED] Core 1 HardFault via `get_bootsel_button()` disabling Flash XiP**
* **Location:** `src/main.cpp`, `src/config/preset_store.cpp`
* **Issue:** Flash erase/program and BOOTSEL button polling disable the flash Execute-in-Place (XiP) hardware. If Core 1 attempts to fetch an instruction from flash during this window, it hard-faults.
* **Fix:** Use `multicore_lockout_start_blocking()` and `multicore_lockout_end_blocking()` around these operations to safely pause Core 1.


**[FIXED] Inter-Core Race Condition (Deadlock) in Audio Rendering**
* **Location:** `src/synth/supersaw.cpp`, `src/main.cpp`
* **Issue:** Core 1 resets `core1RenderCmd = 0` after finishing. If Core 0 assigns a new command before Core 1 clears it, the new command is overwritten with 0, causing Core 0 to deadlock waiting for Core 1.
* **Fix:** Remove `supersaw.core1RenderCmd = 0;` from Core 1. Core 0 should exclusively manage this variable.

**[CRITICAL] Missing Memory Barrier for IPC Audio Buffers**
* **Location:** `src/main.cpp`
* **Issue:** Core 1 writes audio samples to `core1ScratchBuf` and flags `core1RenderDone = true`. Without a memory barrier, Core 0 might see the flag before the memory writes are committed.
* **Fix:** Add `__dmb()` or `__compiler_memory_barrier()` before setting the done flag.

**[HIGH] `load()` Mutates Output Parameter on Failure**
* **Location:** `src/config/preset_store.cpp`
* **Issue:** Data is copied directly from flash into the caller's `Preset` object before validation. If validation fails, the caller is left with garbage data.
* **Fix:** Read into a temporary buffer and only copy to the output upon successful validation.

**[HIGH] Fixed-Point Parameter Smoothing Asymmetry (Gets Stuck)**
* **Location:** `src/synth/supersaw.cpp`
* **Issue:** `currentMix += (targetMix - currentMix) >> 6;` gets stuck because right-shifting a negative integer rounds toward negative infinity. Knobs like Mix and Detune cannot reach 100%.
* **Fix:** Store values in a higher precision format (e.g., Q16.16) internally.

**[HIGH] Allpass Filter Glitch via Truncation Wrap-around**
* **Location:** `src/synth/chorus.cpp`
* **Issue:** Interpolation overshoot in `wetL` is cast to `int16_t` without clamping, causing wrap-around glitches and loud spikes.
* **Fix:** Clamp `wetL` and `wetR` to `[-32768, 32767]` prior to casting.

**[MEDIUM] Uninitialized Struct Padding Written to Flash**
* **Location:** `src/config/preset_store.h`
* **Issue:** `sizeof(Preset)` is 20 bytes due to struct padding, not 17 bytes. Writing this struct to flash copies uninitialized stack memory.
* **Fix:** Use `__attribute__((packed))` on the `Preset` struct.

**[MEDIUM] Incomplete Code Generation in `generate_tables.py`**
* **Location:** `scripts/generate_tables.py`
* **Issue:** The script only prints `midiNotePhaseInc` and `detuneMultiplier` arrays to stdout instead of persisting them to a `.cpp` or `.h` file.

**[MEDIUM] Missing UTF-8 Encoding during File Writes**
* **Location:** `scripts/*.py`
* **Issue:** Python scripts write C++ files containing Unicode characters without specifying `encoding="utf-8"`, which can crash on Windows (`cp1252`).

**[LOW] Silent Event Dropping on MIDI Queue Overflow**
* **Location:** `src/midi/midi_input.cpp`
* **Issue:** If the MIDI queue overflows, events are silently dropped. Dropping a `NOTE_OFF` event leads to permanently hanging notes.
* **Fix:** Force `NOTE_OFF` messages into the queue or trigger an "All Notes Off" panic state.

---

## 2. Performance Issues

* **64-bit Integer Math in the Audio Loop:** In `src/synth/filter.cpp`, `(1LL * R * s1_L) >> 14` forces slow 64-bit software multiplication. Fix by right-shifting `s1_L` before multiplying: `R * (s1_L >> 14)`.
* **Hardware Division in the Audio Loop:** In `src/synth/chorus.cpp`, the fractional delay calculation uses division, blocking the CPU to use the hardware divider twice per sample. Fix by precomputing into a lookup table.
* **Suboptimal `memcpy` Inside a Hardware Spinlock:** In `src/synth/supersaw.cpp`, `cacheAcquire` holds an interrupt-disabling spinlock while executing a 4KB `memcpy` from flash, stalling the other core. Copy to a scratch buffer first.
* **Missing Build Optimizations:** `CMakeLists.txt` does not set `CMAKE_BUILD_TYPE Release` or add `-O3` flags, causing severe CPU overload for fixed-point math and array lookups.
* **Audio Dropouts During Flash Write:** Saving presets blocks the CPU for tens of milliseconds, preventing I2S buffer refills.
* **Memory Duplication:** `scripts/generate_velocity_table.py` defines a `static const uint16_t` array in a header file, duplicating it in every translation unit. Change to `inline constexpr`.

---

## 3. Structure

* **Redundant Polling vs FIFOs:** Dual-core communication in `src/main.cpp` uses manual `while(true)` spin-loops with `volatile` flags. The Pico SDK's `multicore_fifo_push_blocking()` should be used instead.
* **Misleading Function Scope:** `Supersaw::renderVoiceSample` accumulates into output references instead of returning a pair or passing the scratch buffer directly.
* **MIDI Channel Data is Discarded:** In `src/midi/midi_input.cpp`, the MIDI channel is stripped, locking the synth into Omni mode and limiting future multi-timbral expansion.
* **Hardcoded DMA and PIO Allocation:** `src/audio/audio_output.cpp` hardcodes DMA and PIO channels instead of using `dma_claim_unused_channel()`.
* **CMake Globbing Anti-Pattern:** `CMakeLists.txt` uses `file(GLOB_RECURSE)` without `CONFIGURE_DEPENDS`, which fails to detect new files automatically.
* **Fragile Regex File Patching:** `scripts/generate_filter_table.py` modifies `src/synth/filter.cpp` in-place using a brittle regex, prone to breaking on minor formatting changes.

---

## 4. Readability & Comprehension

* **Missing Parameter Smoothing on Cutoff:** Changes to the Cutoff MIDI CC trigger massive numerical leaps in the exponential table, generating audible clicks.
* **Constant Obfuscation in `StereoChorus`:** The magic number `64u << 24` is used for a 90° phase offset. It should be defined as a named constant.
* **Magic Numbers in Event Packing:** `MidiEvent::pack()` uses raw integer shifts and masks instead of named bitfield constants.
* **Misleading Comment Regarding Struct Size:** The comment in `preset_store.h` incorrectly claims the struct is 17 bytes instead of its padded 20 bytes.
* **Misleading Dead Code in Filter Generation:** A defensive check for `fc > 22000.0` in `generate_filter_table.py` is mathematically unreachable and misleads the reader.
* **Undocumented Script Alignment:** `generate_velocity_table.py` is not documented in `AGENTS.md`, leaving new developers unaware it needs to be run.