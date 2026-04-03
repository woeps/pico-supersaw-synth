#include "synth/filter.h"

namespace synth {

// Piecewise-exponential cutoff coefficients (Q14):
//   CC   0–80:  20 Hz → 8000 Hz  (exponential)
//   CC 80–127: 8000 Hz → 16000 Hz  (exponential, finer resolution)
// coeff = tan(π × fc / 44100) × 16384
const int32_t filterCutoffTable[128] = {
       23,    25,    27,    29,    31,    34,    37,    39,
       42,    46,    49,    53,    57,    62,    67,    72,
       77,    83,    90,    97,   104,   113,   121,   131,
      141,   152,   164,   176,   190,   205,   221,   238,
      256,   276,   298,   321,   346,   373,   402,   433,
      467,   503,   543,   585,   630,   679,   732,   789,
      851,   917,   988,  1066,  1149,  1238,  1335,  1439,
     1552,  1674,  1805,  1946,  2099,  2265,  2443,  2636,
     2845,  3071,  3317,  3582,  3871,  4184,  4525,  4897,
     5303,  5747,  6235,  6771,  7363,  8020,  8752,  9573,
    10499, 10696, 10898, 11105, 11318, 11537, 11762, 11993,
    12231, 12476, 12728, 12988, 13256, 13532, 13817, 14111,
    14416, 14730, 15056, 15393, 15742, 16104, 16480, 16871,
    17277, 17700, 18141, 18600, 19080, 19581, 20106, 20656,
    21233, 21840, 22478, 23152, 23862, 24615, 25412, 26259,
    27161, 28123, 29153, 30257, 31446, 32730, 34120, 35631,
};

void SVFilter::init() {
    s1_L = s2_L = 0;
    s1_R = s2_R = 0;
    cutoffCoeff = filterCutoffTable[127]; // wide open
    dampCoeff = 32768;                    // Q=0.5 (no resonance)
    int32_t g = cutoffCoeff;
    int32_t R = dampCoeff;
    D = (1 << 14) + ((1LL * R * g) >> 14) + ((1LL * g * g) >> 14);
    invD = ((1 << 28) + D / 2) / D;       // Q14 reciprocal of D
    mode = FilterMode::LPF;
}

void SVFilter::setCutoff(uint8_t cc) {
    if (cc > 127) cc = 127;
    cutoffCoeff = filterCutoffTable[cc];
    int32_t g = cutoffCoeff;
    int32_t R = dampCoeff;
    D = (1 << 14) + ((1LL * R * g) >> 14) + ((1LL * g * g) >> 14);
    invD = ((1 << 28) + D / 2) / D;
}

void SVFilter::setResonance(uint8_t cc) {
    if (cc > 127) cc = 127;
    // Linear map: CC 0 → damp=2.0 (Q=0.5), CC 127 → damp=0.125 (Q≈8.0).
    // The Q limit of ~8.0 is required because the filter becomes numerically
    // unstable in fixed-point arithmetic at higher Q values (quantization noise
    // causes continuous self-oscillation at extreme resonance).
    // Q14: 32768 − (cc × 30720) / 127
    dampCoeff = 32768 - (static_cast<int32_t>(cc) * 30720) / 127;
    int32_t g = cutoffCoeff;
    int32_t R = dampCoeff;
    D = (1 << 14) + ((1LL * R * g) >> 14) + ((1LL * g * g) >> 14);
    invD = ((1 << 28) + D / 2) / D;
}

void SVFilter::setMode(uint8_t cc) {
    if (cc <= 42) {
        mode = FilterMode::LPF;
    } else if (cc <= 84) {
        mode = FilterMode::BPF;
    } else {
        mode = FilterMode::HPF;
    }
}

void SVFilter::process(int16_t& left, int16_t& right) {
    // Scale input to Q28: >>1 for 2× headroom, <<14 for 14 extra
    // fractional bits.  Net shift: <<13.  This eliminates the
    // truncation dead-zone that silenced the LP output at low cutoff.
    int32_t in_l = static_cast<int32_t>(left)  << 13;
    int32_t in_r = static_cast<int32_t>(right) << 13;

    int32_t g = cutoffCoeff;   // Q14
    int32_t R = dampCoeff;     // Q14

    // ---- Left channel ZDF Trapezoidal SVF (Q28 internals) ----
    int64_t hp_num_L = (int64_t)in_l
                     - ((1LL * R * s1_L) >> 14)
                     - ((1LL * g * s1_L) >> 14)
                     - s2_L;
    int32_t hp_L = static_cast<int32_t>((hp_num_L * invD) >> 14);
    int32_t v1_L = static_cast<int32_t>(((int64_t)g * hp_L) >> 14);
    int32_t bp_L = v1_L + s1_L;
    int32_t v2_L = static_cast<int32_t>(((int64_t)g * bp_L) >> 14);
    int32_t lp_L = v2_L + s2_L;

    s1_L += 2 * v1_L;
    if (s1_L >  STATE_MAX) s1_L =  STATE_MAX;
    if (s1_L < -STATE_MAX) s1_L = -STATE_MAX;
    s2_L += 2 * v2_L;
    if (s2_L >  STATE_MAX) s2_L =  STATE_MAX;
    if (s2_L < -STATE_MAX) s2_L = -STATE_MAX;

    // ---- Right channel ZDF Trapezoidal SVF (Q28 internals) ----
    int64_t hp_num_R = (int64_t)in_r
                     - ((1LL * R * s1_R) >> 14)
                     - ((1LL * g * s1_R) >> 14)
                     - s2_R;
    int32_t hp_R = static_cast<int32_t>((hp_num_R * invD) >> 14);
    int32_t v1_R = static_cast<int32_t>(((int64_t)g * hp_R) >> 14);
    int32_t bp_R = v1_R + s1_R;
    int32_t v2_R = static_cast<int32_t>(((int64_t)g * bp_R) >> 14);
    int32_t lp_R = v2_R + s2_R;

    s1_R += 2 * v1_R;
    if (s1_R >  STATE_MAX) s1_R =  STATE_MAX;
    if (s1_R < -STATE_MAX) s1_R = -STATE_MAX;
    s2_R += 2 * v2_R;
    if (s2_R >  STATE_MAX) s2_R =  STATE_MAX;
    if (s2_R < -STATE_MAX) s2_R = -STATE_MAX;

    // Select output based on filter mode and scale back to int16.
    // Undo the <<13 input scaling.
    int32_t outL, outR;
    switch (mode) {
        case FilterMode::HPF:
            outL = hp_L >> 13;
            outR = hp_R >> 13;
            break;
        case FilterMode::BPF:
            outL = bp_L >> 13;
            outR = bp_R >> 13;
            break;
        default: // LPF
            outL = lp_L >> 13;
            outR = lp_R >> 13;
            break;
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
