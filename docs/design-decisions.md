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

This prevents MIDI input from being starved when audio rendering is busy waiting for buffers. Inter-core communication uses the RP2040's hardware multicore FIFO — a lock-free, single-producer/single-consumer queue that avoids the overhead of mutexes. Each MIDI event is packed into a single 32-bit FIFO word.

## 7 Oscillators × 4 Voices

The classic Roland JP-8000 supersaw uses 7 oscillators. With 4-voice polyphony, up to 28 oscillators run simultaneously. At 44100 Hz with integer math, the RP2040 at 125 MHz has sufficient headroom: each sample requires ~28 wavetable lookups plus envelope and panning, well within the ~2835 cycles available per sample.

## ADSR Envelope

Each voice has an independent ADSR envelope with stages: IDLE → ATTACK → DECAY → SUSTAIN → RELEASE → IDLE.

The envelope level is a `uint32_t` accumulator (full 32-bit range, 0 to `0xFFFF0000`). Per-sample increments are precomputed from CC values using a quadratic time mapping (CC 0 → ~2 ms, CC 127 → ~2 s). The upper 16 bits of the level are used as a multiplier applied to the voice output, keeping the per-sample cost to a single 32×32 integer multiply.

This replaces the PoC's simple 5 ms fade ramp.

## Voice Allocation & Stealing

With 4-voice polyphony, incoming notes are assigned to free voices. If all voices are active, the oldest voice is stolen. Each voice carries a monotonic age counter to determine the steal target. Retriggering an already-playing note restarts the envelope from the current level. Oscillator phases are never reset — they free-run continuously for organic variation between note attacks.

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

## Hybrid Waveform Generation

A hybrid approach balances JP-8000 authenticity with practical constraints at 44.1 kHz:

- **Notes below C5 (MIDI 0–71):** Naive sawtooth from the raw phase accumulator. Aliasing harmonics land above ~16 kHz and add the characteristic bright "air" of the JP-8000. This path is also cheaper than wavetable lookup.
- **Notes C5 and above (MIDI 72–127):** Band-limited wavetables prevent harsh audible aliasing. 11 octave bands generated via additive synthesis with harmonics limited below Nyquist. Tables stored in flash (~44 KB).

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

## Stereo Chorus

The JP-8000 generates its supersaw in mono and creates stereo width via a downstream chorus effect. This implementation adds a stereo chorus after the voice mix stage. Two delay lines (L and R) are modulated by a triangle LFO with 90° phase offset between channels. The LFO waveform is pre-computed as a 256-entry lookup table stored in flash (512 bytes), indexed by a 32-bit phase accumulator — no runtime branching for waveform generation. Delay buffers are 1024 samples (power-of-two for efficient bitmask wrapping), consuming ~4.1 KB RAM. The chorus depth defaults to 0 (dry/bypass) so existing patches are unaffected until CC 91 is sent.
