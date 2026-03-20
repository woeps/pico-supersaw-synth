# Supersaw Algorithm

## Overview

A supersaw is a rich, detuned sound created by layering multiple sawtooth oscillators at slightly different frequencies. This implementation uses 7 oscillators — the classic count from the Roland JP-8000.

## Oscillator Layout

```
Osc 0: center - 3/3 × detune
Osc 1: center - 2/3 × detune
Osc 2: center - 1/3 × detune
Osc 3: center (base frequency)
Osc 4: center + 1/3 × detune
Osc 5: center + 2/3 × detune
Osc 6: center + 3/3 × detune
```

The detune amount is controlled via MIDI CC 94 (0–0.5 semitones). The 6 outer oscillators are spaced evenly around the center. At CC 0 all oscillators play at the same frequency; at CC 127 the outermost oscillators are ±0.5 semitones from center.

## Phase Accumulator

Each oscillator uses a 32-bit phase accumulator. On every sample:

```
phase += phaseIncrement
```

The phase wraps naturally at 2^32.

## Band-Limited Wavetable Synthesis

Rather than using a naive sawtooth (which has infinite harmonics and causes aliasing), this implementation uses pre-computed band-limited wavetables. Each wavetable contains one cycle of a sawtooth wave with harmonics limited to below the Nyquist frequency (22050 Hz) for its frequency range.

### Octave Bands

11 octave bands cover MIDI notes 0–127. Each band uses a different wavetable optimized for that frequency range:

- Band 0: notes 0–11 (lowest frequencies, most harmonics)
- Band 10: notes 120–127 (highest frequencies, fewest harmonics)

### Wavetable Lookup

The phase accumulator (32-bit) is used to index into the wavetable:

```
idx  = phase >> 21        // top 11 bits → table index (0..2047)
frac = (phase >> 5) & 0xFFFF  // next 16 bits → fractional part

saw = s0 + ((s1 - s0) * frac) >> 16  // linear interpolation
```

Linear interpolation between adjacent samples smooths the output and reduces quantization artifacts. The wavetables are stored in flash (not RAM) as `const` data.

## Phase Increment Calculation

The phase increment determines the oscillator's frequency:

```
phaseIncrement = (frequency / sampleRate) × 2^32
```

A precomputed lookup table stores phase increments for all 128 MIDI notes. Detune is applied by interpolating between unity (65536) and a precomputed max-offset per oscillator (`detuneMaxOffset[]`), scaled by the current detune CC value. The resulting multiplier is applied via Q16.16 fixed-point multiply.

## MIDI Note to Frequency

Standard tuning: A4 (MIDI note 69) = 440 Hz.

```
frequency = 440 × 2^((note - 69) / 12)
```

## Stereo Spread

The 7 oscillators have fixed pan offsets: {−3, −2, −1, 0, +1, +2, +3}. The spread parameter (CC 93) scales these into L/R gain pairs (Q8.8 fixed-point). At spread = 0 all oscillators output equally to both channels (mono). At spread = 127 the outermost oscillators are hard-panned.

Per-oscillator panning is applied during rendering:

```
voiceL += (saw × panL[osc]) >> 8
voiceR += (saw × panR[osc]) >> 8
```

## Mixing and Normalization

Per voice, the 7 oscillator outputs are summed per channel and divided by 7:

```
normalized = (sum × 9362) >> 16
```

Where 9362/65536 ≈ 1/7 ≈ 0.1429.

After applying the ADSR envelope, all 4 voices are summed and divided by 4 (`>> 2`) to prevent clipping at full polyphony.

## ADSR Envelope

Each voice has an independent ADSR envelope that replaces the PoC's simple fade ramp. The envelope level is a `uint32_t` accumulator (0 to `0xFFFF0000`). Per-sample increments are precomputed from CC values (attack: CC 73, decay: CC 75, sustain: CC 79, release: CC 72) using a quadratic time mapping.

The upper 16 bits of the level are used as a multiplier:

```
envMul = envLevel >> 16
voiceL = (voiceL × envMul) >> 16
```

When a voice's envelope reaches IDLE after release, the voice is marked inactive and freed for reuse.
