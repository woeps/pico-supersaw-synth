# Supersaw Algorithm

## Overview

A supersaw is a rich, detuned sound created by layering multiple sawtooth oscillators at slightly different frequencies. This implementation uses 7 oscillators — the classic count from the Roland JP-8000 — with several characteristics modeled after the original hardware.

## Oscillator Layout

The 7 oscillators use **asymmetric detune coefficients** derived from the JP-8000 firmware:

```
Osc 0: center × (1 + offset[0])    offset = −10667  (widest negative)
Osc 1: center × (1 + offset[1])    offset =  −6108
Osc 2: center × (1 + offset[2])    offset =  −1893
Osc 3: center (base frequency)     offset =      0
Osc 4: center × (1 + offset[4])    offset =  +1893
Osc 5: center × (1 + offset[5])    offset =  +6048
Osc 6: center × (1 + offset[6])    offset = +10430  (widest positive)
```

The positive/negative pairs are intentionally unequal (e.g. +6048 vs. −6108, +10430 vs. −10667). This asymmetry prevents perfect harmonic cancellation between oscillator pairs and produces richer, less periodic beating — a key characteristic of the JP-8000 sound.

The detune amount is controlled via MIDI CC 94. A non-linear curve maps the CC value to the actual detune amount (see [Detune Curve](#detune-curve)).

## Phase Accumulator

Each oscillator uses a 32-bit phase accumulator. On every sample:

```
phase += phaseIncrement
```

The phase wraps naturally at 2^32.

Oscillator phases are **free-running** — they are never reset on note-on. This means each note trigger starts with whatever phase the oscillators happen to be at, producing organic variation between note attacks. This matches the JP-8000's behavior and avoids the mechanical, identical-sounding attacks that phase reset produces.

## Waveform Generation

A hybrid approach is used depending on the note being played:

### Naive Saw (MIDI notes 0–71, below C5)

For notes below C5 (523 Hz), the raw phase accumulator is used directly as the waveform:

```
saw = (phase >> 17) - 16384    // 15-bit centered sawtooth
```

This "naive" sawtooth includes aliasing harmonics that fold back from the Nyquist frequency. At 44.1 kHz with fundamentals below ~500 Hz, these aliases land above ~16 kHz and are mostly inaudible, but they add the bright, shimmery "air" characteristic of the JP-8000's sound. This path is also computationally cheaper than wavetable lookup.

### Band-Limited Wavetable (MIDI notes 72–127, C5 and above)

For higher notes where aliasing would produce audible artifacts, pre-computed band-limited wavetables are used. 11 octave bands cover the full MIDI range, each with harmonics limited below Nyquist:

```
idx  = phase >> 21                    // top 11 bits → table index (0..2047)
frac = (phase >> 5) & 0xFFFF          // next 16 bits → fractional part
saw  = s0 + ((s1 - s0) * frac) >> 16  // linear interpolation
```

The wavetables are stored in flash as `const` data. At `noteOn()` time, the active octave band is copied into an SRAM cache to avoid XIP flash cache thrashing during rendering (see [Wavetable SRAM Cache](#wavetable-sram-cache)).

## Wavetable SRAM Cache

With 3–4 voices playing high notes simultaneously, each using a different octave band (4 KB per band), the working set exceeds the RP2040's 16 KB XIP flash cache, causing frequent cache misses (~10–30 cycle stalls per random read).

To eliminate this, active wavetable bands are copied from flash into SRAM at `noteOn()` time. A shared cache with reference counting ensures:

- **Deduplication:** Two voices on the same octave band share a single SRAM copy.
- **Automatic eviction:** When all voices release a band, the cache slot is freed.
- **Flash fallback:** If the cache is full (4 slots), rendering falls back to flash reads.

The cache holds up to 4 bands (4 × 4 KB = 16 KB). SRAM reads are 1–2 cycles with zero cache miss penalty.

## Detune Curve

The CC 94 value is mapped through a **non-linear piecewise curve** before being applied as the detune amount. This matches the JP-8000's behavior: fine control at low settings (the musical "sweet spot") with an exponential ramp at high values.

```
CC   0– 63:  +1 every 2 CC steps    (fine control)
CC  64– 80:  +1 every CC step       (moderate)
CC  81–120:  +2 every CC step       (accelerating)
CC 121–127:  rapid ramp to maximum  (dramatic)
```

The curve output ranges 0–255, giving higher resolution than the raw 0–127 CC value.

## Mix Control

The mix parameter (CC 95) controls the balance between the center oscillator and the six side oscillators, modeled after the JP-8000:

- **Center oscillator (index 3):** Fixed gain of approximately 0.195 (50/256 in Q8.8), regardless of mix setting.
- **Side oscillators (indices 0–2, 4–6):** Gain scales from 0 to 1.0 based on the mix CC value.

At mix = 0, only the center oscillator is audible (thin, single-saw tone). At mix = 127, all oscillators contribute fully (classic thick supersaw).

## Parameter Smoothing

The mix and detune parameters use **one-pole exponential slew** to prevent zipper noise during CC sweeps:

```
currentValue += (targetValue - currentValue) >> 6    // ~1 ms slew @ 44.1 kHz
```

CC changes write to a target value; the render loop smoothly interpolates toward it each sample. Detune phase increment recalculation is amortized to every 32 samples (~0.7 ms) to spread the cost of 7 multiplies × 4 voices.

## Phase Increment Calculation

The phase increment determines the oscillator's frequency:

```
phaseIncrement = (frequency / sampleRate) × 2^32
```

A precomputed lookup table stores phase increments for all 128 MIDI notes. Detune is applied by interpolating between unity (65536) and the asymmetric max-offset per oscillator (`detuneMaxOffset[]`), scaled by the smoothed detune amount (0–255). The resulting multiplier is applied via Q16.16 fixed-point multiply.

## MIDI Note to Frequency

Standard tuning: A4 (MIDI note 69) = 440 Hz.

```
frequency = 440 × 2^((note - 69) / 12)
```

## High-Pass Filter

Each voice has a **DC-blocking high-pass filter** applied after the oscillator sum but before the envelope. This removes DC offset and sub-fundamental energy, matching the JP-8000's signal chain.

The filter is a single-pole HPF with cutoff ~20 Hz:

```
y[n] = α × (y[n-1] + x[n] - x[n-1])
α ≈ 0.9986 (16361 in Q14 fixed-point)
```

Q14 format keeps all intermediates within `int32_t` range (worst-case: 16361 × 84000 = 1.37B < 2.15B), avoiding expensive `int64_t` multiplications on the M0+ core.

Both L and R channels are filtered independently per voice.

## Stereo Spread

The 7 oscillators have fixed pan offsets: {−3, −2, −1, 0, +1, +2, +3}. The spread parameter (CC 93) scales these into L/R gain pairs (Q8.8 fixed-point). At spread = 0 all oscillators output equally to both channels (mono). At spread = 127 the outermost oscillators are hard-panned.

Per-oscillator panning is applied during rendering together with the mix gain:

```
voiceL += (saw × gain × panL[osc]) >> 16
voiceR += (saw × gain × panR[osc]) >> 16
```

## ZDF Filter

A global 2-pole Zero-Delay Feedback (ZDF) Trapezoidal state-variable filter (SVF) is applied after voice mixing, before the stereo chorus. Currently, only the high-pass (HPF) output is mapped, but the filter inherently computes low-pass (LPF) and band-pass (BPF) signals internally.

### Signal Path

```
Voice Mix (÷4) → Clamp → ZDF Filter → Stereo Chorus → I2S Output
```

### Implementation

The filter uses the ZDF Trapezoidal topology, bypassing oversampling requirements by leveraging the SIO hardware divider on the RP2040 for absolute stability at high cutoff frequencies and resonance up to the Nyquist limit:

```
hp_num = input - (R * s1) - (g * s1) - s2
hp = hp_num / D_half    // Evaluated in hardware using 8 cycles
v1 = g * hp
bp = v1 + s1
v2 = g * bp

s1 += 2 * v1
s2 += 2 * v2
```

Coefficients and math use Q14 fixed-point or combinations thereof. State variables (`s1`, `s2`) are clamped to ±32767 after each iteration to prevent `int32_t` overflow. The core division utilizes the RP2040 `hw_divider_quotient_s32`, keeping the performance cost to a minimum.

The cutoff coefficient `g` is precomputed as a 128-entry flash lookup table mapping CC values to `tan(π × fc / 44100) × 16384`, with `fc` exponentially mapped from 20 Hz to 16 kHz. This avoids runtime trigonometry.

The denominator (`D = 1 + 2Rg + g^2`) is precomputed whenever the Cutoff or Resonance MIDI CCs are modified, avoiding evaluation on every audio sample.

### MIDI Control

- **CC 74 (Filter Cutoff):** 0 = 20 Hz, 127 = 16 kHz. Default: 127.
- **CC 71 (Filter Resonance):** 0 = flat (Q=0.5), 127 = high resonance (Q≈8.0). Default: 0. *(Note: Q is capped at ~8.0 because the fixed-point filter topology becomes numerically unstable and self-oscillates at more extreme Q values)*.
- **CC 70 (Filter Mode):** 0–42 = LPF, 43–84 = BPF, 85–127 = HPF. Default: LPF. (Note: Only HPF is currently active on output).

### Memory & CPU

- **RAM:** 4 × `int32_t` state variables + 3 × `int32_t` coefficients = 28 bytes
- **Flash:** 128 × `int32_t` cutoff table = 512 bytes
- **CPU:** ~18 instructions per channel, featuring 1 fast hardware division.

## Stereo Chorus

A stereo chorus effect is applied to the final mixed output (after the SVF filter), modeled after the JP-8000's approach where stereo width comes from a downstream chorus rather than per-oscillator panning.

### Signal Path

```
Voice Mix (÷4) → Clamp → SVF Filter → Stereo Chorus → I2S Output
```

The chorus coexists with the per-oscillator spread — spread positions the oscillators in the stereo field, while the chorus adds modulated stereo widening on top.

### Implementation

Two modulated delay lines (L and R) with:

- **Delay center:** 10 ms (441 samples)
- **Delay sweep:** ±5 ms (±220 samples), giving a 5–15 ms range
- **LFO:** Pre-computed 256-entry triangle wave table (stored in flash, 512 bytes). A 32-bit phase accumulator indexes the table, with **8-bit linear interpolation** between adjacent entries to eliminate staircase artifacts in the delay modulation.
- **L/R phase offset:** 90° (64 entries apart in the 256-entry table)
- **Allpass interpolation** on delay buffer reads for smooth, artifact-free modulation. The first-order allpass (`a = (1 − d) / (1 + d)`) provides unity gain at all frequencies and a flat group delay, avoiding the time-varying low-pass filtering inherent in linear interpolation. The coefficient division uses the RP2040 hardware divider.
- **Additive wet/dry mix:** The dry signal is always present and the wet (delayed) signal is added on top, scaled by the depth parameter. Output is clamped to `int16_t` range. This ensures true chorus thickening at all depth settings, rather than crossfading into vibrato.

### MIDI Control

- **CC 91 (Chorus Depth):** Wet amount added to dry signal. 0 = fully dry (bypass), 127 = maximum wet added. Default: 0.
- **CC 92 (Chorus Rate):** LFO rate. 0 = ~0.1 Hz, 127 = ~3.0 Hz. Default: ~1 Hz.

### Memory

- **RAM:** 2 × 1024 × 2 bytes (delay buffers, power-of-two for bitmask wrapping) + LFO state + 2 × `int16_t` allpass state ≈ 4.1 KB
- **Flash:** 256 × 2 bytes (triangle LFO table) = 512 bytes

## Mixing and Normalization

Per voice, the 7 oscillator outputs are summed per channel with per-oscillator gain (center vs. side), then normalized by the maximum effective gain:

```
normalized = (sum × 10579) >> 16
```

Where 10579/65536 ≈ 1/6.195, accounting for the center gain (0.195) plus 6 side oscillators at full gain.

After applying the ADSR envelope and high-pass filter, all 4 voices are summed and divided by 4 (`>> 2`) to prevent clipping at full polyphony.

## ADSR Envelope

Each voice has an independent ADSR envelope. The envelope level is a `uint32_t` accumulator (0 to `0xFFFF0000`). Per-sample increments are precomputed from CC values (attack: CC 73, decay: CC 75, sustain: CC 79, release: CC 72) using a quadratic time mapping.

The upper 16 bits of the level are used as a multiplier:

```
envMul = envLevel >> 16
voiceL = (voiceL × envMul) >> 16
```

When a voice's envelope reaches IDLE after release, the voice is marked inactive and freed for reuse.

## Velocity Sensitivity

Each voice stores its MIDI velocity (0–127) at note-on time. The velocity is mapped to a gain value through a **logarithmic curve** stored as a precomputed 128-entry lookup table (`velocityGainTable` in `velocity_table.h`).

### Curve

The table uses a power function matching human loudness perception, with a 30% minimum gain floor so soft notes remain audible:

```
gain = (0.30 + 0.70 × (velocity / 127) ^ 0.4) × 256
```

In Q8 fixed-point (256 = unity gain). Representative values:

| Velocity | Gain (Q8) | Percentage |
|----------|-----------|------------|
| 0        | 0         | 0% (silent) |
| 1        | 103       | 40%        |
| 32       | 180       | 70%        |
| 64       | 213       | 83%        |
| 100      | 240       | 94%        |
| 127      | 256       | 100%       |

The 30% floor ensures that even the softest notes retain bass presence and audibility. Velocity 0 remains silent (MIDI convention for note-off).

### Application

Velocity gain is applied per-voice, per-sample, after the ADSR envelope multiply:

```
velGain = velocityGainTable[voice.velocity]    // Q8
voiceL = (voiceL × velGain) >> 8
voiceR = (voiceR × velGain) >> 8
```

### CPU Cost

One table lookup + two multiplies + two shifts per voice per sample. The table (256 bytes) resides in flash.

### Table Generation

The lookup table is generated by `scripts/generate_velocity_table.py`, which writes `src/synth/velocity_table.h`. Re-run the script if the curve exponent or scale needs adjustment.
