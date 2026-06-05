#include "synth/filter.h"
#include "synth/filter_cutoff_table.h"

namespace synth {

void SVFilter::init() {
    s1_L = s2_L = 0;
    s1_R = s2_R = 0;
    cutoffCoeff = filterCutoffTable[127]; // wide open
    targetCutoffCoeff = cutoffCoeff;      // smoothing target starts at open value
    dampCoeff = 32768;                    // Q=0.5 (no resonance)
    int32_t g = cutoffCoeff;
    int32_t R = dampCoeff;
    D = (1 << 14) + ((1LL * R * g) >> 14) + ((1LL * g * g) >> 14);
    invD = ((1 << 28) + D / 2) / D;       // Q14 reciprocal of D
    resoCompGain = 16384;                  // Q14 unity (no compensation at Q=0.5)
    mode = FilterMode::LPF;
}

void SVFilter::setCutoff(uint8_t cc) {
    if (cc > 127) cc = 127;
    // Only set the smoothing target; cutoffCoeff slews toward it in
    // tickSmoothing() so abrupt CC changes don't cause zipper noise.
    targetCutoffCoeff = filterCutoffTable[cc];
}

void SVFilter::tickSmoothing() {
    int32_t delta = targetCutoffCoeff - cutoffCoeff;
    if (delta == 0) return;
    // One-pole slew toward the target. Snap when the remaining delta is tiny
    // to avoid a long crawl of single-LSB updates.
    if (delta > -64 && delta < 64) {
        cutoffCoeff = targetCutoffCoeff;
    } else {
        cutoffCoeff += delta >> 6;
    }
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
    // Resonance gain compensation: attenuate input by 1/Q when Q > 1
    // to prevent the resonance peak from exceeding 0 dB.
    // dampCoeff = 2R (Q14), Q = 1/(2R) = 16384/dampCoeff.
    // When Q <= 1 (dampCoeff >= 16384): no attenuation.
    // When Q > 1 (dampCoeff < 16384): attenuate by dampCoeff/16384.
    resoCompGain = (dampCoeff < 16384) ? dampCoeff : 16384;
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
    // Scale input to Q28 with resonance gain compensation.
    // Original: left << 13 (net shift for Q28 headroom).
    // With compensation: (left * resoCompGain) >> 1, because
    // resoCompGain is Q14, so left * Q14 >> (14-13) = >> 1.
    // At unity (resoCompGain=16384): (left * 16384) >> 1 = left << 13.
    int32_t in_l = (static_cast<int32_t>(left)  * resoCompGain) >> 1;
    int32_t in_r = (static_cast<int32_t>(right) * resoCompGain) >> 1;

    int32_t g = cutoffCoeff;   // Q14
    int32_t R = dampCoeff;     // Q14

    // Shift Q28 state down by 14 *before* multiplying by the Q14 coefficient,
    // keeping the products in 32-bit (avoids slow 64-bit software multiplies).
    // s1 max = STATE_MAX (2^28) → s1 >> 14 ≈ 16384; R/g max ≈ 35631
    // → product ≈ 5.8e8, well within int32_t.
    int32_t s1_L_q14 = s1_L >> 14;
    int32_t s1_R_q14 = s1_R >> 14;

    // ---- Left channel ZDF Trapezoidal SVF (Q28 internals) ----
    int64_t hp_num_L = (int64_t)in_l
                     - (R * s1_L_q14)
                     - (g * s1_L_q14)
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
                     - (R * s1_R_q14)
                     - (g * s1_R_q14)
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
