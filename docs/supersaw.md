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

The maximum detune is hardcoded at 0.3 semitones. The 6 outer oscillators are spaced evenly around the center.

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

A precomputed lookup table stores phase increments for all 128 MIDI notes. Detune is applied by multiplying the base phase increment with fixed-point detune multipliers (Q16.16 format).

## MIDI Note to Frequency

Standard tuning: A4 (MIDI note 69) = 440 Hz.

```
frequency = 440 × 2^((note - 69) / 12)
```

## Mixing and Normalization

The 7 oscillator outputs are summed and divided by 7 to prevent clipping. Division by 7 is approximated with a fixed-point multiply:

```
normalized = (sum × 9362) >> 16
```

Where 9362/65536 ≈ 1/7 ≈ 0.1429.

## Click Avoidance

A linear fade ramp (~5ms / 220 samples at 44.1kHz) is applied on note on (fade-in) and note off (fade-out) to avoid audible clicks from abrupt amplitude changes.
