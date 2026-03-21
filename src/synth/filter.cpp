#include "synth/filter.h"

namespace synth {

// Piecewise-exponential cutoff coefficients (Q14):
//   CC   0–80:  20 Hz → 8000 Hz  (exponential)
//   CC 80–127: 8000 Hz → 16000 Hz  (exponential, finer resolution)
// coeff = sin(π × fc / 44100) × 16384
const int16_t filterCutoffTable[128] = {
       23,    25,    27,    29,    31,    34,    37,    39,
       42,    46,    49,    53,    57,    62,    67,    72,
       77,    83,    90,    97,   104,   113,   121,   131,
      141,   152,   164,   176,   190,   205,   221,   238,
      256,   276,   298,   321,   346,   373,   402,   433,
      467,   503,   542,   584,   630,   679,   731,   788,
      850,   916,   987,  1063,  1146,  1235,  1331,  1434,
     1545,  1665,  1794,  1933,  2082,  2243,  2416,  2603,
     2803,  3019,  3251,  3500,  3767,  4054,  4362,  4692,
     5045,  5423,  5827,  6258,  6716,  7203,  7720,  8265,
     8840,  8956,  9074,  9193,  9312,  9433,  9555,  9677,
     9801,  9926, 10051, 10178, 10305, 10433, 10562, 10692,
    10823, 10954, 11086, 11218, 11351, 11485, 11619, 11754,
    11889, 12024, 12159, 12294, 12430, 12566, 12701, 12836,
    12971, 13106, 13240, 13374, 13507, 13639, 13770, 13900,
    14029, 14157, 14283, 14407, 14530, 14651, 14769, 14886,
};

void SVFilter::init() {
    lowL = bandL = 0;
    lowR = bandR = 0;
    cutoffCoeff = filterCutoffTable[127]; // wide open
    dampCoeff = 32768;                    // Q=0.5 (no resonance)
    mode = FilterMode::LPF;
    bypass = true;                        // cutoff=127 + LPF → passthrough
    prevBypass = true;
    crossfadeCount = 0;
}

void SVFilter::setCutoff(uint8_t cc) {
    if (cc > 127) cc = 127;
    cutoffCoeff = filterCutoffTable[cc];
    bypass = (cc >= 125) && (mode == FilterMode::LPF);
}

void SVFilter::setResonance(uint8_t cc) {
    if (cc > 127) cc = 127;
    // Linear map: CC 0 → damp=2.0 (Q=0.5), CC 127 → damp=0.05 (Q≈20).
    // Q14: 32768 − (cc × 31949) / 127
    dampCoeff = 32768 - (static_cast<int32_t>(cc) * 31949) / 127;
}

void SVFilter::setMode(uint8_t cc) {
    if (cc <= 42) {
        mode = FilterMode::LPF;
    } else if (cc <= 84) {
        mode = FilterMode::BPF;
    } else {
        mode = FilterMode::HPF;
    }
    // Recalculate bypass: LPF with near-open cutoff (≥125)
    bypass = (cutoffCoeff >= filterCutoffTable[125]) && (mode == FilterMode::LPF);
}

void SVFilter::process(int16_t& left, int16_t& right) {
    // Detect bypass transitions and start a crossfade (32 samples ≈ 0.7 ms).
    if (bypass != prevBypass) {
        crossfadeCount = 32;
        if (!bypass) {
            // bypass → active: seed state from current input to avoid
            // stale-state transient (replaces the old all-zero guard).
            lowL  = static_cast<int32_t>(left)  >> 1;
            lowR  = static_cast<int32_t>(right) >> 1;
            bandL = bandR = 0;
        }
        prevBypass = bypass;
    }

    if (bypass && crossfadeCount == 0) return;

    // Attenuate input by 1 bit before the SVF to give state variables
    // 2× headroom before hitting the ±32767 clamp.  This eliminates
    // hard-clipping distortion at high cutoff coefficients.
    int32_t in_l = static_cast<int32_t>(left)  >> 1;
    int32_t in_r = static_cast<int32_t>(right) >> 1;

    int32_t f = cutoffCoeff;
    int32_t d = dampCoeff;
    int32_t highL, highR;

    // Double-sampled Chamberlin SVF: two iterations per sample for
    // stability at high cutoff frequencies (fc up to 16 kHz).
    // State is clamped to ±32767 after each iteration to prevent
    // int32_t overflow in subsequent multiplications.
    for (int iter = 0; iter < 2; iter++) {
        // Left channel
        lowL  += (f * bandL) >> 14;
        if (lowL >  32767) lowL =  32767;
        if (lowL < -32768) lowL = -32768;
        highL  = in_l - lowL - ((d * bandL) >> 14);
        bandL += (f * highL) >> 14;
        if (bandL >  32767) bandL =  32767;
        if (bandL < -32768) bandL = -32768;

        // Right channel
        lowR  += (f * bandR) >> 14;
        if (lowR >  32767) lowR =  32767;
        if (lowR < -32768) lowR = -32768;
        highR  = in_r - lowR - ((d * bandR) >> 14);
        bandR += (f * highR) >> 14;
        if (bandR >  32767) bandR =  32767;
        if (bandR < -32768) bandR = -32768;
    }

    // Select output based on filter mode and scale back up (+1 bit)
    int32_t outL, outR;
    switch (mode) {
        case FilterMode::HPF:
            outL = highL << 1;
            outR = highR << 1;
            break;
        case FilterMode::BPF:
            outL = bandL << 1;
            outR = bandR << 1;
            break;
        default: // LPF
            outL = lowL << 1;
            outR = lowR << 1;
            break;
    }

    // Crossfade between filtered and dry signal on bypass transitions
    if (crossfadeCount > 0) {
        int32_t dryL = static_cast<int32_t>(left);
        int32_t dryR = static_cast<int32_t>(right);
        if (bypass) {
            // active → bypass: fade from filtered to dry
            int32_t wet = crossfadeCount;  // 32..1
            int32_t dry = 32 - wet;
            outL = (outL * wet + dryL * dry) >> 5;
            outR = (outR * wet + dryR * dry) >> 5;
        } else {
            // bypass → active: fade from dry to filtered
            int32_t wet = 32 - crossfadeCount;  // 1..32
            int32_t dry = crossfadeCount;
            outL = (outL * wet + dryL * dry) >> 5;
            outR = (outR * wet + dryR * dry) >> 5;
        }
        crossfadeCount--;
    }

    // Clamp output to int16_t range
    if (outL >  32767) outL =  32767;
    if (outL < -32768) outL = -32768;
    if (outR >  32767) outR =  32767;
    if (outR < -32768) outR = -32768;

    left  = static_cast<int16_t>(outL);
    right = static_cast<int16_t>(outR);
}

} // namespace synth
