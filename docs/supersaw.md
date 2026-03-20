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

The wavetables are stored in flash (not RAM) as `const` data.

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
α ≈ 0.9986 (65444 in Q16 fixed-point)
```

Both L and R channels are filtered independently per voice.

## Stereo Spread

The 7 oscillators have fixed pan offsets: {−3, −2, −1, 0, +1, +2, +3}. The spread parameter (CC 93) scales these into L/R gain pairs (Q8.8 fixed-point). At spread = 0 all oscillators output equally to both channels (mono). At spread = 127 the outermost oscillators are hard-panned.

Per-oscillator panning is applied during rendering together with the mix gain:

```
voiceL += (saw × gain × panL[osc]) >> 16
voiceR += (saw × gain × panR[osc]) >> 16
```

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
