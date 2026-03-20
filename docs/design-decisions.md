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

With 4-voice polyphony, incoming notes are assigned to free voices. If all voices are active, the oldest voice is stolen. Each voice carries a monotonic age counter to determine the steal target. Retriggering an already-playing note resets its phase and restarts the envelope from the current level.

## Sample Rate: 44100 Hz

Standard CD-quality sample rate. The PCM5102A supports both 44100 and 48000 Hz. 44100 Hz was chosen as the conventional default and requires slightly less CPU per second.

## Stereo Spread

The 7 oscillators are assigned fixed pan positions: {−3, −2, −1, 0, +1, +2, +3}. The `spread` parameter (CC 93) scales these positions from mono center (spread = 0) to full stereo (spread = 127). Per-oscillator L/R gains are stored in Q8.8 fixed-point and recomputed only when the CC value changes.

## Runtime Detune

Detune is no longer hardcoded. The `detune` parameter (CC 94) interpolates each oscillator's frequency multiplier between unity (no detune) and a maximum offset of 0.5 semitones. Precomputed max-offset values per oscillator (`detuneMaxOffset[]`) keep the per-noteOn cost to 7 integer multiplies.

## MIDI CC Configuration

All CC number assignments are centralized in `src/config/midi_cc.h` as `#define` constants. This makes remapping parameters a single-file change with no logic edits required.

| CC | Parameter | Range |
|----|-----------|-------|
| 73 | Attack | 0–127 → ~2 ms – 2 s |
| 75 | Decay | 0–127 → ~2 ms – 2 s |
| 79 | Sustain | 0–127 → 0–100% level |
| 72 | Release | 0–127 → ~2 ms – 2 s |
| 94 | Detune | 0–127 → 0–0.5 semitones |
| 93 | Spread | 0–127 → mono to full stereo |

## Band-Limited Wavetable Synthesis

A naive sawtooth waveform has infinite harmonics that extend beyond the Nyquist frequency, causing aliasing (audible as harsh artifacts at high frequencies). Instead of oversampling or complex anti-aliasing filtering, this implementation uses pre-computed band-limited wavetables.

11 octave bands cover MIDI notes 0–127. Each wavetable is generated via additive synthesis with harmonics limited to below 22050 Hz for its frequency range. At render time, linear interpolation between table samples provides smooth playback. The tables are stored in flash (not RAM), using ~44 KB of the 2 MB available.
