# JP-8000 Supersaw Emulation Plan for RP2040

This document evaluates the current supersaw implementation, compares it to the
Roland JP-8000's original algorithm, highlights the differences, and provides a
step-by-step plan for getting as close as possible to the original sound within
the constraints of the RP2040.

---

## 1 — Current Implementation Summary

### Algorithm Overview

```
MIDI Note ─► Phase-Inc Table (128 entries)
                  │
                  ▼
          Detune Module (7 symmetric offsets)
                  │
      ┌───┬───┬───┼───┬───┬───┐
      ▼   ▼   ▼   ▼   ▼   ▼   ▼
    Osc0 Osc1 Osc2 Osc3 Osc4 Osc5 Osc6   ← band-limited wavetable lookup
      │   │   │   │   │   │   │              with linear interpolation
      └───┴───┴───┼───┴───┴───┘
                  ▼
        Per-oscillator stereo pan
                  │
                  ▼
         Sum & Normalize (÷7)
                  │
                  ▼
           ADSR Envelope
                  │
                  ▼
      Voice mix (÷4 for polyphony)
                  │
                  ▼
          Clamp → I2S DAC (44.1 kHz stereo)
```

### Key Steps

| Step | Detail |
|---|---|
| **Waveform** | Band-limited sawtooth via precomputed wavetables (2048 samples × 11 octave bands). Harmonics truncated at Nyquist per band via additive synthesis. |
| **Phase accumulator** | 32-bit unsigned integer per oscillator. `phase += phaseInc` every sample; natural wrap-around produces the saw cycle. |
| **Wavetable lookup** | Upper 11 bits index the table; 16-bit fractional part drives linear interpolation between adjacent samples. |
| **Detune** | 7 oscillators with **symmetric** offsets `{−1, −2/3, −1/3, 0, +1/3, +2/3, +1}` scaled by 0.5 semitones max. CC 0–127 linearly interpolates between unison and max detune. |
| **Stereo spread** | Per-oscillator pan offsets `{−3,−2,−1,0,+1,+2,+3}` scaled by the `spread` CC value. L/R gain in Q8.8. |
| **Mixing** | All 7 oscillators summed at equal weight, divided by 7. No per-oscillator amplitude control. |
| **Envelope** | Linear ADSR with quadratic CC-to-time mapping (2 ms – 2000 ms). Applied per-voice after summation. |
| **Polyphony** | 4 voices, hard-divided (`>>2`) before clamp. Oldest-voice stealing. |
| **Output** | 44.1 kHz, 16-bit stereo I2S to PCM5102A DAC. |

### Key Parameters Affecting Sound

| Parameter | Control | Effect |
|---|---|---|
| `detuneAmount` (CC 94) | 0–127, linear | Width of frequency spread. 0 = unison; 127 = ±0.5 semitones. |
| `spread` (CC 93) | 0–127 | Stereo image width. 0 = mono, 127 = hard-panned outers. |
| Attack / Decay / Sustain / Release | CC 73/75/79/72 | ADSR shape. Quadratic time mapping. |
| Wavetable band | Automatic per note | Selects harmonic count to avoid aliasing. |

---

## 2 — The Roland JP-8000 Supersaw Algorithm

Based on Adam Szabo's 2010 thesis *"How to Emulate the Super Saw"*, the 39C3
reverse-engineering by The Usual Suspects, the "A to Synth" blog's confirmed
DSP code analysis, and spectral analysis from ghostfact.com.

### JP-8000 Architecture

```
MIDI Note ─► Pitch (24-bit accumulator value for 88.2 kHz)
                  │
Detune CC ──► Piecewise-linear exponential curve
                  │
                  ▼
          detuneBase = (pitch × detune) >> 23
                  │
                  ▼
          Asymmetric coefficients
          {0, +128, −128, +408, −412, +704, −720}
                  │
      ┌───┬───┬───┼───┬───┬───┐
      ▼   ▼   ▼   ▼   ▼   ▼   ▼
    Saw0 Saw1 Saw2 Saw3 Saw4 Saw5 Saw6   ← naive (aliasing) 24-bit accumulators
      │               │
      │ sides         │ center
      ▼               ▼
  ×(mix>>16)>>7    ×25>>7  (≈ 0.195 fixed)
      │               │
      └───────┬───────┘
              ▼
      Σ → High-Pass Filter
              │
              ▼
     Downstream DSP (normalization / filtering / chorus)
              │
              ▼
         DAC @ 88.2 kHz
```

### JP-8000 Key Characteristics

| Aspect | Detail |
|---|---|
| **Sample rate** | 88.2 kHz (2× oversampled). |
| **Oscillator type** | **Naive (aliasing) sawtooth** — 24-bit accumulator overflow. No band-limiting. Aliasing is a deliberate part of the character. |
| **Detune coefficients** | **Asymmetric**: `{0, +128, −128, +408, −412, +704, −720}`. Positive/negative pairs are intentionally unequal to prevent perfect cancellation and add organic beating. |
| **Detune curve** | **Piecewise-linear, exponential-ish** — fine control at low settings, dramatic ramp in the last ~25% of travel. |
| **Mix control** | Center oscillator at **fixed** attenuation (≈0.195). Side oscillators controlled by the mix parameter. The curves Szabo measured are an emergent effect of downstream normalization. |
| **High-pass filter** | On the summed output. Removes DC offset and sub-fundamental aliasing artifacts. |
| **Phase** | **Free-running** — oscillators are never reset on note-on. Creates ever-varying phase relationships. |
| **Parameter smoothing** | Mix and detune are slewed in the DSP; changes are gradual. |

---

## 3 — Key Differences

| Feature | This Codebase | JP-8000 | Sonic Impact |
|---|---|---|---|
| **Waveform** | Band-limited wavetable (anti-aliased) | Naive accumulator (aliasing) @ 88.2 kHz | JP's aliasing gives bright, shimmery "air." Band-limited tables are cleaner but less characteristically JP. **Major.** |
| **Detune coefficients** | Symmetric, uniformly spaced | Asymmetric, non-uniformly spaced | JP's asymmetry prevents harmonic alignment; richer, less periodic beating. **Moderate.** |
| **Detune curve** | Linear CC mapping | Piecewise-linear, exponential-ish | JP gives fine control in the sweet spot; linear wastes range. **Significant.** |
| **Mix control** | None — equal weight | Center fixed, sides variable via CC | Missing an entire dimension of timbral control. **Major.** |
| **High-pass filter** | None | Single-pole HPF on summed output | Shapes low end, removes DC wander. **Moderate.** |
| **Phase on note-on** | Reset to 0 | Free-running (never reset) | Reset creates mechanical, identical attacks. Free-running adds organic variation. **Moderate.** |
| **Sample rate** | 44.1 kHz | 88.2 kHz | JP pushes aliasing above audible range for low notes. **Architectural constraint.** |
| **Param smoothing** | Instant CC changes | Slewed transitions | Instant causes zipper noise on sweeps. **Minor–moderate.** |
| **Stereo model** | Per-oscillator panning | Mono supersaw → stereo chorus | JP stereo width comes from chorus, not oscillator panning. **Structural.** |
| **Polyphony** | 4 voices | 8 voices | Limits chords but acceptable for leads. **Hardware constraint.** |

---

## 4 — Missing Features for Faithful JP-8000 Emulation

1. **No mix control** — the JP's mix is one of its two primary timbral controls.
   Without it, the sound is always "full supersaw" with no way to dial in a
   cleaner single-saw tone.

2. **No high-pass filter** — essential for removing DC offset and
   sub-fundamental energy, especially with naive saws.

3. **Phase reset on note-on** — the JP uses free-running oscillators. Current
   code resets all phases to zero, producing identical attacks.

4. **Wrong detune coefficients** — symmetric and uniformly spaced instead of
   asymmetric and non-uniform.

5. **Wrong detune curve** — linear instead of exponential/piecewise. The usable
   "sweet spot" is compressed into a tiny CC range.

6. **No parameter smoothing** — mix and detune changes are instant (zipper
   noise).

7. **No aliasing character** — band-limited wavetables remove the harmonic
   "air" that defines the JP sound. (Trade-off: aliasing at 44.1 kHz can
   sound harsher than the JP's 88.2 kHz.)

8. **Wrong stereo model** — JP supersaw is mono; stereo width comes from a
   downstream chorus effect. Current per-oscillator panning is a different
   approach.

9. **No downstream signal chain** — the JP-8000 has a resonant filter
   (LPF/HPF/BPF), a second oscillator, ring/cross modulation, LFO, and
   chorus. These all shape the final sound significantly.

---

## 5 — RP2040 Constraints

| Resource | Spec | Relevance |
|---|---|---|
| CPU | Dual Cortex-M0+, 133 MHz, no FPU, no HW MAC | Integer/fixed-point only. 64-bit intermediates are expensive. |
| RAM | 264 KB SRAM | Wavetables currently ~44 KB. Room for delay buffers and tables. |
| Flash | 2 MB+ (board dependent) | Plenty for lookup tables. |
| Audio budget | 44.1 kHz × 4 voices × 7 oscs ≈ 1.23 M osc ticks/sec → ~108 cycles per osc tick at 133 MHz | Tight. Every operation per sample counts. |

---

## 6 — Step-by-Step Plan

### Phase 1 — Zero-Cost Fixes

These changes cost nothing in CPU and take under an hour each.

#### Step 1: Free-Running Phase

Remove `memset(phase, 0, sizeof(phase))` from `noteOn()`. Leave oscillator
phases wherever they were from the previous note, or seed them with a simple
LFSR on first allocation. This immediately adds organic variation between
note triggers.

**Files:** `src/synth/supersaw.cpp` — `Supersaw::noteOn()`

**CPU cost:** Saves cycles (removes a memset).

#### Step 2: Asymmetric Detune Coefficients

Replace `detuneMaxOffset[7]` with values derived from the actual JP
coefficients `{0, +128, −128, +408, −412, +704, −720}`.

The JP's `detuneBase = (pitch × detune) >> 23` produces a base offset, then
each coefficient scales it. Normalized to Q16.16 multiplier offsets at max
detune, approximate target values:

```c
static const int16_t detuneMaxOffset[NUM_OSCILLATORS] = {
    0, +1893, -1893, +6048, -6108, +10430, -10667
};
```

> Exact values should be calibrated by comparing spectral output against JP
> recordings at known detune settings.

**Files:** `src/synth/supersaw.cpp` — `detuneMaxOffset[]`

**CPU cost:** Zero.

#### Step 3: Non-Linear Detune Curve

Replace the linear `detuneAmount` mapping with a 128-entry lookup table stored
in flash. The table implements the JP's piecewise-linear curve:

```
CC   0:        detune = 1
CC   0– 63:    increment by 1 every 2 steps
CC  64– 80:    increment by 1 every step
CC  81–120:    increment by 2 every step
CC 121–123:    increment by 8 every step
CC 124:        increment by 16
CC 125:        increment by 32
CC 126:        increment by 96
```

The `setCC(CC_DETUNE, value)` handler indexes this table to get the actual
detune amount instead of using the CC value directly.

```c
// In flash
static const uint8_t detuneCurve[128] = { /* precomputed */ };

// In setCC:
detuneAmount = detuneCurve[value];
```

**Files:** `src/synth/supersaw.cpp` — `Supersaw::setCC()`

**CPU cost:** Negligible (one table lookup per CC change).

---

### Phase 2 — Add Mix Control

#### Step 4: Mix Parameter (Center vs. Sides)

Add a `mixAmount` field (0–127) to `Supersaw`, controlled by a new CC (e.g.,
CC 95).

In `render()`, apply different gains to the center oscillator (index 3) vs.
the six side oscillators:

```cpp
// Center: fixed gain ≈ 25/128 in Q8.8
static constexpr int32_t CENTER_GAIN = 50; // 50/256 ≈ 0.195

// Side gain derived from mixAmount
int32_t sideGain = (static_cast<int32_t>(mixAmount) * 256) / 127;

for (int osc = 0; osc < NUM_OSCILLATORS; osc++) {
    // ... wavetable lookup → saw ...
    int32_t gain = (osc == 3) ? CENTER_GAIN : sideGain;
    voiceL += (saw * gain * static_cast<int32_t>(panL[osc])) >> 16;
    voiceR += (saw * gain * static_cast<int32_t>(panR[osc])) >> 16;
}
```

Adjust the normalization divisor to account for the new gain structure.

**Files:** `src/synth/supersaw.h`, `src/synth/supersaw.cpp`,
`src/config/midi_cc.h`

**CPU cost:** ~2 extra multiplies per oscillator per sample (minimal).

#### Step 5: Parameter Smoothing

Add one-pole exponential slew for detune and mix to prevent zipper noise:

```cpp
// Per-sample, before use in render():
currentDetune += (targetDetune - currentDetune) >> 6; // ~1 ms slew @ 44.1 kHz
currentMix    += (targetMix    - currentMix)    >> 6;
```

Store `currentDetune` and `currentMix` as 32-bit values for smooth
interpolation. `setCC()` writes to `target*`; `render()` slews toward it.

**Files:** `src/synth/supersaw.h`, `src/synth/supersaw.cpp`

**CPU cost:** ~4 arithmetic ops per sample (not per oscillator).

---

### Phase 3 — High-Pass Filter

#### Step 6: Single-Pole HPF Per Voice

Add a DC-blocking high-pass filter after summing the 7 oscillators but before
the envelope. Cutoff ~20–30 Hz:

```cpp
// y[n] = α · (y[n-1] + x[n] - x[n-1])
// α ≈ 0.995 → Q16 value: 65209
static constexpr int32_t HPF_ALPHA = 65209;

// Per voice, per channel:
hpfStateL = (int32_t)((int64_t)HPF_ALPHA * (hpfStateL + voiceL - prevL) >> 16);
prevL = voiceL;
voiceL = hpfStateL;
```

Add `int32_t hpfStateL, hpfStateR, prevL, prevR` to `Voice`.

**Files:** `src/synth/supersaw.h`, `src/synth/supersaw.cpp`

**CPU cost:** ~4 multiply-adds per voice per sample. Uses `int64_t`
intermediates which are expensive on M0+ (~6–8 cycles each) but still
manageable at 4 voices.

---

### Phase 4 — Aliasing Character (Optional / Experimental)

#### Step 7: Hybrid Naive-Saw Mode

Add a compile-time or runtime flag to bypass the wavetable for lower notes and
output the raw phase accumulator as the sample:

```cpp
#ifdef NAIVE_SAW_MODE
    int32_t saw = (int32_t)(voice.phase[osc] >> 17) - 16384; // 15-bit centered
#else
    // existing wavetable lookup + interpolation
#endif
```

At 44.1 kHz (vs. JP's 88.2 kHz), aliasing is more aggressive. Mitigate with a
hybrid approach:

- **Notes below C5 (MIDI 72):** Use naive saws. Aliasing harmonics land above
  ~16 kHz and are less audible.
- **Notes C5 and above:** Keep band-limited wavetables to avoid harsh
  high-frequency artifacts.

```cpp
if (voice.note < 72) {
    saw = (int32_t)(voice.phase[osc] >> 17) - 16384;
} else {
    // wavetable path
}
```

This is computationally **cheaper** than wavetable lookup (saves ~8 ops/osc)
and captures much of the JP's brightness.

**Files:** `src/synth/supersaw.cpp` — `Supersaw::render()`

**CPU cost:** Negative (saves cycles vs. wavetable).

---

### Phase 5 — Stereo Chorus

#### Step 8: Mono Supersaw + Stereo Chorus

Replace per-oscillator panning with the JP-8000's approach: mono supersaw into
a stereo chorus effect.

1. Sum all 7 oscillators to a single mono value.
2. Feed into two modulated delay lines (one L, one R):
   - Delay time: ~5–15 ms, modulated by a slow triangle LFO (~0.5–2 Hz).
   - L and R LFOs are 90° out of phase.
3. Mix dry + wet.

**Memory requirements:**
- Delay buffer: `44100 × 0.015 = 662 samples × 2 bytes ≈ 1.3 KB` per channel.
  Two channels ≈ 2.6 KB. Fits easily in RAM.
- LFO sine table: 256 entries × 2 bytes = 512 bytes.

**Implementation sketch:**

```cpp
struct StereoChorus {
    int16_t delayBufL[662];
    int16_t delayBufR[662];
    uint16_t writeIdx;
    uint32_t lfoPhase;
    uint32_t lfoInc; // controls rate

    void process(int16_t monoIn, int16_t& outL, int16_t& outR);
};
```

This replaces the current `panL[]/panR[]` system entirely.

**Files:** New `src/synth/chorus.h`, `src/synth/chorus.cpp`;
modify `src/synth/supersaw.cpp`

**CPU cost:** Moderate (~10 ops per sample for LFO + interpolated delay read).

---

### Phase 6 — Polish & Extended Signal Chain (Stretch Goals)

#### Step 9: Resonant Low-Pass Filter

The JP-8000's filter is central to classic supersaw patches. A 2-pole
state-variable filter (SVF) is efficient on Cortex-M0+:

- ~10 multiply-adds per sample per voice.
- Controllable via CC for cutoff and resonance.
- Switchable LPF / HPF / BPF modes.

At 4 voices this adds ~40 multiplies per sample. Consider processing every
2nd sample if the budget is tight.

**Files:** New `src/synth/filter.h`, `src/synth/filter.cpp`

#### Step 10: Velocity Sensitivity

Currently `velocity` is ignored in `noteOn()`. Map it to:
- Envelope amplitude (simple multiply), and/or
- Filter cutoff (more expressive).

**Files:** `src/synth/supersaw.cpp` — `noteOn()`, `render()`

**CPU cost:** Zero (one extra multiply on note-on).

#### Step 11: Increase Polyphony

After switching to naive saws (saves ~56 ops/voice), there may be headroom
for 6 voices instead of 4. Profile actual cycle counts first.

**Files:** `src/synth/supersaw.h` — `MAX_VOICES`

---

## 7 — Priority Matrix

| # | Change | Effort | Sonic Impact | CPU Cost |
|---|---|---|---|---|
| 1 | Free-running phase | Trivial | Medium | Saves cycles |
| 2 | Asymmetric detune coefficients | Trivial | Medium | Zero |
| 3 | Non-linear detune curve | Easy | High | Negligible |
| 4 | Mix control | Easy | **High** | Minimal |
| 5 | Parameter smoothing | Easy | Medium | Minimal |
| 6 | High-pass filter | Easy | Medium | Low |
| 7 | Naive saw mode (hybrid) | Medium | **High** | Saves cycles |
| 8 | Stereo chorus | Medium | **High** | Moderate |
| 9 | Resonant filter | Medium | High | Moderate |
| 10 | Velocity sensitivity | Trivial | Low | Zero |
| 11 | More polyphony | Easy | Low | N/A |

**Steps 1–6** bring the implementation substantially closer to the JP-8000
character with minimal CPU overhead and a few hours of work.

**Steps 7–8** close most of the remaining gap at moderate effort and represent
the highest-value changes for sonic authenticity.

**Steps 9–11** move toward a more complete JP-8000 voice architecture rather
than just a supersaw oscillator.

---

## References

- Adam Szabo, *"How to Emulate the Super Saw"*, KTH Bachelor Thesis, 2010.
  <https://www.adamszabo.com/internet/adam_szabo_how_to_emulate_the_super_saw.pdf>
- "The Usual Suspects", 39C3 — Reverse-engineering the JP-80x0 Toshiba DSP.
  <https://www.youtube.com/watch?v=XM_q5T7wTpQ&t=1804s>
- Joakim / "A to Synth" blog, *"The super saw code"*, February 2026.
  <https://atosynth.blogspot.com/2026/02/the-super-saw-code.html>
- Ghost Fact, *"Frontier Days of Software Synthesis: Exploring the JP-8000 Supersaw"*.
  <https://www.ghostfact.com/jp-8000-supersaw/>
- Alex Shore, *"An Analysis of Roland's Super Saw Oscillator"*, 2013.
