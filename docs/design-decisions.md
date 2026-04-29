# Design Decisions

## Fixed-Point Arithmetic in the Render Loop

The RP2040 has no hardware floating-point unit. All float operations are emulated in software, making them ~10-20x slower than integer operations. The audio render loop runs at 44100 Hz and must process 7 oscillators per sample, so performance is critical.

All render-loop math uses:
- **32-bit phase accumulators** for oscillator state
- **Q16.16 fixed-point** for detune multipliers
- **Integer multiply + shift** for division by 7

Float is never used. Phase increments come from a precomputed lookup table, detune multipliers are interpolated at `noteOn()` time using integer math, and the ADSR envelope uses a full-range `uint32_t` accumulator.

## pico_audio_i2s (pico-extras)

The SDK's `pico_audio_i2s` library from pico-extras handles the low-level I2S protocol via PIO and DMA. This provides:
- Double/triple buffered audio output
- Automatic DMA transfers
- Correct I2S timing

The alternative (raw PIO I2S) would give more control but significantly more code for no benefit at this stage.

## Dual-Core Architecture

- **Core 0**: Audio rendering (time-critical, blocks on DMA buffer availability)
- **Core 1**: MIDI polling (latency-sensitive, must not be blocked by audio)

This prevents MIDI input from being starved when audio rendering is busy waiting for buffers. Inter-core communication uses a lock-free, single-producer/single-consumer software ring buffer (256 elements deep) that avoids the overhead of mutexes and prevents the cores from deadlocking if the queue temporarily fills during rendering. Each MIDI event is packed into a single 32-bit word.

## 7 Oscillators × 4 Voices

The classic Roland JP-8000 supersaw uses 7 oscillators. With 4-voice polyphony, up to 28 oscillators run simultaneously. At 44100 Hz with integer math, the RP2040 at 125 MHz has sufficient headroom: each sample requires ~28 wavetable lookups plus envelope and panning, well within the ~2835 cycles available per sample.

## ADSR Envelope

Each voice has an independent ADSR envelope with stages: IDLE → ATTACK → DECAY → SUSTAIN → RELEASE → IDLE.

The envelope level is a `uint32_t` accumulator (full 32-bit range, 0 to `0xFFFF0000`). Per-sample increments are precomputed from CC values using a quadratic time mapping (CC 0 → ~2 ms, CC 127 → ~2 s). The upper 16 bits of the level are used as a multiplier applied to the voice output, keeping the per-sample cost to a single 32×32 integer multiply.

The RELEASE stage includes an explicit `level == 0` guard at its entry: if the envelope is already at zero when RELEASE begins (e.g. sustain CC = 0), it immediately transitions to IDLE rather than waiting for the next `releaseInc` subtraction. Without this guard, a voice could remain `active = true` indefinitely while outputting silence.

This replaces the PoC's simple 5 ms fade ramp.

## Voice Allocation & Stealing

With 4-voice polyphony, incoming notes are assigned to free voices. If all voices are active, a voice is stolen using the following priority:

1. **Prefer voices in RELEASE stage** — the one with the lowest remaining envelope level is stolen first, minimising the audible click.
2. **Fall back to oldest ATTACK/DECAY/SUSTAIN voice** — determined by the monotonic age counter.

Previously, only the oldest voice was stolen regardless of envelope stage. This meant stealing an actively-sounding voice when a quieter RELEASE voice was available. The updated steal order also ensures `noteOff` for the stolen note is never silently discarded, because a RELEASE voice is already decaying — it would naturally reach IDLE on its own.

Retriggering an already-playing note restarts the envelope from the current level. Oscillator phases are never reset — they free-run continuously for organic variation between note attacks.

`noteOff()` releases **all** voices holding the given note number (previously only the first match was released). This prevents a voice from hanging when the same note occupies two slots after a steal-and-retrigger sequence.

## Sample Rate: 44100 Hz

Standard CD-quality sample rate. The PCM5102A supports both 44100 and 48000 Hz. 44100 Hz was chosen as the conventional default and requires slightly less CPU per second.

## Stereo Spread

The 7 oscillators are assigned fixed pan positions: {−3, −2, −1, 0, +1, +2, +3}. The `spread` parameter (CC 93) scales these positions from mono center (spread = 0) to full stereo (spread = 127). Per-oscillator L/R gains are stored in Q8.8 fixed-point and recomputed only when the CC value changes.

## Runtime Detune

The `detune` parameter (CC 94) is mapped through a non-linear piecewise curve (128-entry lookup table) before being applied. This gives fine control at low detune values and an exponential ramp at high values, matching the JP-8000's behavior. The curve output (0–255) interpolates each oscillator's frequency multiplier between unity and its asymmetric max-offset. The asymmetric coefficients are derived from the JP-8000 firmware — positive/negative pairs are intentionally unequal to prevent perfect cancellation and produce richer beating.

## MIDI CC Configuration

All CC number assignments are centralized in `src/config/midi_cc.h` as `#define` constants. This makes remapping parameters a single-file change with no logic edits required.

| CC | Parameter | Range |
|----|-----------|-------|
| 73 | Attack | 0–127 → ~2 ms – 2 s |
| 75 | Decay | 0–127 → ~2 ms – 2 s |
| 79 | Sustain | 0–127 → 0–100% level |
| 72 | Release | 0–127 → ~2 ms – 2 s |
| 94 | Detune | 0–127 → non-linear curve, 0–max detune |
| 93 | Spread | 0–127 → mono to full stereo |
| 95 | Mix | 0–127 → center only to full supersaw |
| 91 | Chorus Depth | 0–127 → dry to full wet |
| 92 | Chorus Rate | 0–127 → ~0.1–3.0 Hz LFO rate |
| 74 | Filter Cutoff | 0–127 → 20 Hz–16 kHz (piecewise-exponential) |
| 71 | Filter Resonance | 0–127 → Q 0.5–20 |
| 70 | Filter Mode | 0–42=LPF, 43–84=BPF, 85–127=HPF |

## Hybrid Waveform Generation

A hybrid approach balances JP-8000 authenticity with practical constraints at 44.1 kHz:

- **Notes below C5 (MIDI 0–71):** Naive sawtooth from the raw phase accumulator. Aliasing harmonics land above ~16 kHz and add the characteristic bright "air" of the JP-8000. This path is also cheaper than wavetable lookup.
- **Notes C5 and above (MIDI 72–127):** Band-limited wavetables prevent harsh audible aliasing. 11 octave bands generated via additive synthesis with harmonics limited below Nyquist. Tables are normalized to ±16383 (matching the naive saw's amplitude range) to ensure a seamless loudness crossover. Tables stored in flash (~44 KB).

The JP-8000 uses naive saws at 88.2 kHz, pushing aliasing further above the audible range. At 44.1 kHz, the hybrid threshold at C5 is a practical compromise.

## JP-8000 Emulation

Several design choices are specifically modeled after the Roland JP-8000 supersaw oscillator:

- **Free-running phase:** Oscillator phases are never reset on note-on, producing organic variation between attacks.
- **Asymmetric detune:** Coefficients derived from JP firmware. Unequal positive/negative pairs prevent periodic beating.
- **Non-linear detune curve:** Piecewise-linear LUT gives fine control at low settings, exponential ramp at high values.
- **Mix control:** Center oscillator at fixed ~0.195 gain, sides controlled by CC 95. Matches the JP's two primary timbral controls (detune + mix).
- **Parameter smoothing:** One-pole slew on mix and detune prevents zipper noise during CC sweeps (~1 ms time constant).
- **DC-blocking HPF:** Single-pole high-pass filter (~20 Hz) per voice removes DC offset and sub-fundamental energy. Uses Q14 fixed-point to stay within `int32_t` range on M0+.
- **Naive saw character:** Raw phase accumulator for low notes adds the bright, shimmery aliasing characteristic of the JP-8000.
- **Stereo chorus:** Post-mix stereo chorus with modulated delay lines, matching the JP-8000's approach of mono supersaw → stereo chorus for width.

## Wavetable SRAM Cache

When 3–4 voices play notes ≥ C5 simultaneously, each accesses a different 4 KB octave band in flash. The combined ~12–16 KB working set thrashes the RP2040's 16 KB XIP cache, causing 10–30 cycle stalls per random wavetable read.

At `noteOn()` time, the required octave band is copied from flash into an SRAM cache (4 slots × 4 KB = 16 KB max). A reference-counted shared design ensures multiple voices on the same band share one copy, and slots are freed automatically when all referencing voices go idle. If the cache is full, rendering falls back to direct flash reads. SRAM reads complete in 1–2 cycles with no cache miss penalty.

## ZDF Trapezoidal Filter

A global 2-pole Zero-Delay Feedback (ZDF) Trapezoidal state-variable filter is applied post-mix, before the stereo chorus. This placement (single instance rather than per-voice) keeps the CPU cost minimal while still providing the classic resonant filter sweep central to supersaw patches. The SVF topology inherently computes low-pass (LPF), band-pass (BPF), and high-pass (HPF) outputs simultaneously from the same difference equation. A `switch` on the `FilterMode` enum selects which output is routed to the audio path, so all three modes share the same computation with zero additional cost. The mode is selected via MIDI CC 70 (0–42 = LPF, 43–84 = BPF, 85–127 = HPF).

The filter completely bypasses oversampling. The ZDF topology guarantees perfect stability up to the Nyquist limit even at high resonance and cutoff frequencies.

Internally, the filter operates in **Q28 fixed-point** (14 extra fractional bits above the Q14 cutoff coefficients). Input samples are scaled up by `<< 13` (equivalent to `>> 1` headroom attenuation followed by `<< 14`), and the output is scaled back by `>> 13`. State variables (`s1`, `s2`) are stored as `int32_t` in Q28, clamped to `±STATE_MAX` (`(1 << 28) - 1`). This wide internal precision eliminates the truncation dead-zone that previously silenced the low-pass output at very low cutoff values — at CC 0 (`g = 23` in Q14), the product `g × bp` would truncate to zero after the `>> 14` shift when `bp` was stored at Q14 scale, starving the second integrator entirely.

The per-sample division (`hp = hp_num / D`) uses a **precomputed reciprocal multiply** instead of the hardware divider. `invD = round(2²⁸ / D)` is computed once in `setCutoff()` / `setResonance()`, then the per-sample division becomes a 64-bit multiply-and-shift: `hp = (int64_t)hp_num * invD >> 14`. This is both faster (~5 cycles vs. 8 for `hw_divider_quotient_s32`) and avoids the 32-bit numerator overflow that would occur with Q28-scaled `hp_num`. Several intermediate products (`R × s1`, `g × hp`, `g × bp`) also use `int64_t` to prevent overflow before the `>> 14` reduction. The cutoff coefficient is stored in a 128-entry flash lookup table to avoid runtime trigonometry.

The CC-to-frequency mapping uses a **piecewise-exponential curve** with a breakpoint at CC 80 (8 kHz). CC 0–80 sweeps 20 Hz → 8 kHz exponentially (~8.6 octaves in 81 steps), while CC 80–127 sweeps 8 kHz → 16 kHz (1 octave in 47 steps). This gives roughly 5× finer resolution in the top octave, where LPF cutoff adjustments are most audible on bright supersaw patches. The table (`filterCutoffTable`) precomputes `tan(π×fc/fs)` in Q14 fixed point.

Resonance is mapped linearly from CC 0 (Q=0.5) to CC 127 (Q≈8.0). The maximum Q is intentionally capped at ~8.0 because the ZDF topology becomes numerically unstable in fixed-point arithmetic at extreme Q values combined with high cutoff frequencies. Beyond this limit, quantization errors in the state variables accumulate and cause continuous self-oscillation (a high-pitched digital whine). Q≈8.0 provides a strong, musical resonant peak while maintaining absolute mathematical stability across the entire cutoff range.

**Resonance gain compensation:** The SVF's LP gain at the resonance frequency is approximately Q. With 4 voices at full amplitude the pre-filter signal already reaches ~100% of int16_t range, so any Q > 1 would hard-clip the output. To prevent this, the filter input is attenuated by `min(1, 1/Q)` via a precomputed `resoCompGain` factor (Q14). When Q ≤ 1 (dampCoeff ≥ 16384) the input passes through at unity; when Q > 1 the input is scaled by `dampCoeff/16384`, keeping the resonance peak at 0 dB. This mirrors the behavior of classic Roland synths (e.g. Juno-106) where the passband drops as resonance increases.

The stable hardware division logic makes bypass and crossfade logic obsolete. The filter computes perfectly and without artifacts even at maximum cutoff limits.

## Stereo Chorus

The JP-8000 generates its supersaw in mono and creates stereo width via a downstream chorus effect. This implementation adds a stereo chorus after the voice mix stage. Two delay lines (L and R) are modulated by a triangle LFO with 90° phase offset between channels. The LFO waveform is pre-computed as a 256-entry lookup table stored in flash (512 bytes), indexed by a 32-bit phase accumulator — no runtime branching for waveform generation. Delay buffers are 1024 samples (power-of-two for efficient bitmask wrapping), consuming ~4.1 KB RAM. The chorus depth defaults to 0 (dry/bypass) so existing patches are unaffected until CC 91 is sent.

The wet/dry mix uses a **crossfade** rather than additive blending: `out = dry × (128 − depth) / 128 + wet × depth / 128`. This guarantees the total gain never exceeds unity regardless of depth, preventing clipping when the pre-chorus signal is already near full scale.

The modulated delay time is computed in Q8 fixed-point (8 fractional bits) so that the sub-sample position used for linear interpolation is derived directly from the delay calculation rather than from unrelated LFO phase bits. This eliminates phase-discontinuity artifacts that would otherwise occur each time the integer delay steps by one sample — particularly audible on higher frequencies where a single-sample jump represents a large phase change.

## Dual-Core Voice Rendering

With all four voices active on high notes (≥ C5), the per-sample cycle budget (~2,834 cycles at 125 MHz / 44.1 kHz) is insufficient for 28 wavetable lookups plus envelopes, HPF, and mixing on a single core. Core 1 was previously dedicated to MIDI polling — idle >99.9% of the time at 31.25 kbaud.

Voice rendering is now split equally across both cores:

- **Core 0:** renders voices 0–1, performs parameter smoothing, merges results, applies chorus
- **Core 1:** renders voices 2–3 into a shared scratch buffer, continues MIDI polling between buffers

This nearly doubles the effective cycle budget to ~5,668 cycles per sample.

### Synchronization

Volatile shared variables coordinate rendering — no FIFO or mutex overhead per buffer. Core 0 writes a sample count to `core1RenderCmd`; Core 1 polls this alongside MIDI and renders when signaled. Core 1 sets `core1RenderDone` on completion; Core 0 spin-waits (typically < 1 µs since both cores do equal work).

### Thread Safety

- **ADSR parameters** are written by Core 0 before render starts and read-only during rendering — no race.
- **Parameter smoothing** (`currentMix`, `currentDetune`) is updated per-sample by Core 0 only. Core 1 reads these atomically (32-bit loads are atomic on Cortex-M0+). The worst case is a one-sample-stale value — inaudible.
- **Detune recalculation** is split: Core 0 updates voices 0–1, Core 1 updates voices 2–3, each using the shared `detuneAmount` (atomic `uint8_t` read).
- **Wavetable SRAM cache** (`bandCache`) and **voice struct mutations** share the same RP2040 hardware spinlock (`cacheLock`). Both cores may call `cacheRelease()` when a voice reaches IDLE during parallel rendering. Core 0's `noteOn()` and `noteOff()` also hold `cacheLock` while writing `note`, `velocity`, `active`, `age`, and `env` fields, preventing Core 1 from reading a partially-written voice struct. `cacheRelease()` acquires the lock internally and is therefore called *before* the lock is re-acquired for voice struct writes to avoid deadlock.

### Memory Cost

Each core has its own `int32_t` scratch buffer (`core0ScratchBuf` and `core1ScratchBuf`, each `int32_t[512]`), adding ~4 KB RAM total. Both buffers use `int32_t` to preserve full dynamic range from partial 2-voice sums — clamping to `int16_t` is deferred until after the final merge and ÷2 normalization, preventing premature hard-clipping when multiple voices are at high amplitude. The ÷2 (not ÷4) is correct because the per-voice `×10579 >> 16` normalization already limits each voice to ~16384 (half of `int16_t` range), so four voices sum to ~65536 and only a single right-shift is needed.

## Preset Persistence via Flash

Synth parameters are persisted to the last 4 KB sector of flash so they survive power cycles. The design stores the 12 raw CC values (one `uint8_t` each) plus a 4-byte magic number and 1-byte version for validation — 17 bytes total, written as a 256-byte flash page (minimum programmable unit). The `Preset` struct uses `__attribute__((packed))` to ensure its size is exactly 17 bytes, preventing uninitialized padding memory from being written to flash.

### BOOTSEL Button as User Control

The Tiny2040's BOOTSEL button is repurposed as a save/restore trigger. It is not a regular GPIO — it shares the QSPI SS line, so reading it requires temporarily overriding the flash chip-select pin and reading the SIO high GPIO register. The reader function is placed in RAM (`__no_inline_not_in_flash_func`) since flash XiP is disabled during the read. This causes a brief (~10 µs) flash pause per poll, which is negligible relative to the ~5.8 ms audio buffer period.

### Button State Machine

A hold-duration state machine runs in the main loop (polled once per audio buffer cycle):

- **< 3 s release** → restore saved preset (blue LED flash)
- **≥ 3 s hold** → red LED blink indicates save mode is armed
- **≥ 5 s hold** → save current parameters (green LED flash)

On boot, `preset_store::load()` checks the flash sector for a valid magic/version header and replays all CC values via `setCC()` before the audio loop starts.

### Flash Write Safety

Flash erase/program requires that no code executes from flash on either core. Core 1 registers as a lockout victim via `multicore_lockout_victim_init()` at startup. Before writing, Core 0 calls `multicore_lockout_start_blocking()` which triggers an NMI on Core 1, parking it in a RAM-resident spin loop. After the write completes, `multicore_lockout_end_blocking()` releases Core 1. The total audio dropout is ~5–10 ms — acceptable for an explicit user-initiated save.
