#include "synth/chorus.h"
#include <cstring>

namespace synth {

// Pre-computed triangle LFO table (256 entries, Q15 unsigned).
// One full cycle: ramps 0 → 32767 over indices 0–127, then 32767 → 0 over 128–255.
// Stored in flash as const data (512 bytes).
const int16_t chorusLfoTable[256] = {
        0,   257,   514,   771,  1028,  1285,  1542,  1799,
     2056,  2313,  2570,  2827,  3084,  3341,  3598,  3855,
     4112,  4369,  4626,  4883,  5140,  5397,  5654,  5911,
     6168,  6425,  6682,  6939,  7196,  7453,  7710,  7967,
     8224,  8481,  8738,  8995,  9252,  9509,  9766, 10023,
    10280, 10537, 10794, 11051, 11308, 11565, 11822, 12079,
    12336, 12593, 12850, 13107, 13364, 13621, 13878, 14135,
    14392, 14649, 14906, 15163, 15420, 15677, 15934, 16191,
    16448, 16705, 16962, 17219, 17476, 17733, 17990, 18247,
    18504, 18761, 19018, 19275, 19532, 19789, 20046, 20303,
    20560, 20817, 21074, 21331, 21588, 21845, 22102, 22359,
    22616, 22873, 23130, 23387, 23644, 23901, 24158, 24415,
    24672, 24929, 25186, 25443, 25700, 25957, 26214, 26471,
    26728, 26985, 27242, 27499, 27756, 28013, 28270, 28527,
    28784, 29041, 29298, 29555, 29812, 30069, 30326, 30583,
    30840, 31097, 31354, 31611, 31868, 32125, 32382, 32639,
    32767, 32510, 32253, 31996, 31739, 31482, 31225, 30968,
    30711, 30454, 30197, 29940, 29683, 29426, 29169, 28912,
    28655, 28398, 28141, 27884, 27627, 27370, 27113, 26856,
    26599, 26342, 26085, 25828, 25571, 25314, 25057, 24800,
    24543, 24286, 24029, 23772, 23515, 23258, 23001, 22744,
    22487, 22230, 21973, 21716, 21459, 21202, 20945, 20688,
    20431, 20174, 19917, 19660, 19403, 19146, 18889, 18632,
    18375, 18118, 17861, 17604, 17347, 17090, 16833, 16576,
    16319, 16062, 15805, 15548, 15291, 15034, 14777, 14520,
    14263, 14006, 13749, 13492, 13235, 12978, 12721, 12464,
    12207, 11950, 11693, 11436, 11179, 10922, 10665, 10408,
    10151,  9894,  9637,  9380,  9123,  8866,  8609,  8352,
     8095,  7838,  7581,  7324,  7067,  6810,  6553,  6296,
     6039,  5782,  5525,  5268,  5011,  4754,  4497,  4240,
     3983,  3726,  3469,  3212,  2955,  2698,  2441,  2184,
     1927,  1670,  1413,  1156,   899,   642,   385,   128,
};

// Delay center: 10 ms in samples = 441
static constexpr uint16_t DELAY_CENTER = 441;

// Delay sweep depth: ±5 ms in samples = ±220
static constexpr uint16_t DELAY_SWEEP = 220;

void StereoChorus::init() {
    memset(delayBufL, 0, sizeof(delayBufL));
    memset(delayBufR, 0, sizeof(delayBufR));
    writeIdx = 0;
    lfoPhase = 0;
    apStateL = 0;
    apStateR = 0;

    // Default: depth = 0 (dry), rate = ~1 Hz
    depth = 0;
    rate = 42;  // ~1 Hz
    setRate(rate);
}

void StereoChorus::setDepth(uint8_t value) {
    depth = value;
}

void StereoChorus::setRate(uint8_t value) {
    rate = value;
    // Map CC 0–127 to ~0.1–3.0 Hz LFO rate.
    // lfoInc = (freq / SAMPLE_RATE) * 2^32
    // freq = 0.1 + (value / 127.0) * 2.9
    // At value=0:   freq=0.1 Hz → lfoInc = 9737
    // At value=127: freq=3.0 Hz → lfoInc = 292119
    // Simplified from: 9737 + (value * (292119 - 9737)) / 127
    // The per-step increment (292119 - 9737) / 127 ≈ 2222.7, rounded to 2223.
    lfoInc = 9737 + (static_cast<uint32_t>(value) * 2223u);
}

void StereoChorus::process(int16_t& left, int16_t& right) {
    // Write input into delay buffers
    delayBufL[writeIdx] = left;
    delayBufR[writeIdx] = right;

    // Advance LFO phase
    lfoPhase += lfoInc;

    // Read LFO value for L channel with interpolation to eliminate staircase.
    // Upper 8 bits index the table, next 8 bits are the interpolation fraction.
    uint8_t idxL = static_cast<uint8_t>(lfoPhase >> 24);
    uint8_t fracLfoL = static_cast<uint8_t>(lfoPhase >> 16);
    int32_t lfoL = chorusLfoTable[idxL]
                 + (((chorusLfoTable[static_cast<uint8_t>(idxL + 1)] - chorusLfoTable[idxL]) * fracLfoL) >> 8);

    // R channel: 90° out of phase (offset by 64 in 256-entry table)
    uint32_t phaseR = lfoPhase + (64u << 24);
    uint8_t idxR = static_cast<uint8_t>(phaseR >> 24);
    uint8_t fracLfoR = static_cast<uint8_t>(phaseR >> 16);
    int32_t lfoR = chorusLfoTable[idxR]
                 + (((chorusLfoTable[static_cast<uint8_t>(idxR + 1)] - chorusLfoTable[idxR]) * fracLfoR) >> 8);

    // Convert LFO (0–32767) to delay in Q8 fixed-point (8 fractional bits).
    // >> 6 instead of >> 14 retains 8 sub-sample bits for smooth interpolation.
    int32_t delayL_q8 = (static_cast<int32_t>(DELAY_CENTER) << 8)
                      + (((static_cast<int32_t>(lfoL) - 16384) * DELAY_SWEEP) >> 6);
    int32_t delayR_q8 = (static_cast<int32_t>(DELAY_CENTER) << 8)
                      + (((static_cast<int32_t>(lfoR) - 16384) * DELAY_SWEEP) >> 6);

    // Split into integer delay and fractional part
    int32_t delayL = delayL_q8 >> 8;
    uint8_t fracL  = static_cast<uint8_t>(delayL_q8 & 0xFF);
    int32_t delayR = delayR_q8 >> 8;
    uint8_t fracR  = static_cast<uint8_t>(delayR_q8 & 0xFF);

    // Clamp integer delay to valid range
    if (delayL < 1) { delayL = 1; fracL = 0; }
    if (delayL >= CHORUS_BUF_SIZE - 1) { delayL = CHORUS_BUF_SIZE - 2; fracL = 0; }
    if (delayR < 1) { delayR = 1; fracR = 0; }
    if (delayR >= CHORUS_BUF_SIZE - 1) { delayR = CHORUS_BUF_SIZE - 2; fracR = 0; }

    // Allpass interpolation: smoother frequency response than linear.
    // Coefficient a = (256 - frac) / (256 + frac), approximated in Q8.
    // y[n] = a * (x[n] - y[n-1]) + x[n-1]
    uint16_t readIdxL0 = (writeIdx - static_cast<uint16_t>(delayL)) & CHORUS_BUF_MASK;
    uint16_t readIdxL1 = (readIdxL0 - 1) & CHORUS_BUF_MASK;
    int32_t xL = static_cast<int32_t>(delayBufL[readIdxL0]);
    int32_t xL1 = static_cast<int32_t>(delayBufL[readIdxL1]);
    int32_t aL = ((256 - fracL) * 256) / (256 + fracL); // Q8 coefficient
    int32_t wetL = (aL * (xL - apStateL) + (xL1 << 8)) >> 8;
    if (wetL > 32767) wetL = 32767;
    if (wetL < -32768) wetL = -32768;
    apStateL = static_cast<int16_t>(wetL);

    uint16_t readIdxR0 = (writeIdx - static_cast<uint16_t>(delayR)) & CHORUS_BUF_MASK;
    uint16_t readIdxR1 = (readIdxR0 - 1) & CHORUS_BUF_MASK;
    int32_t xR = static_cast<int32_t>(delayBufR[readIdxR0]);
    int32_t xR1 = static_cast<int32_t>(delayBufR[readIdxR1]);
    int32_t aR = ((256 - fracR) * 256) / (256 + fracR); // Q8 coefficient
    int32_t wetR = (aR * (xR - apStateR) + (xR1 << 8)) >> 8;
    if (wetR > 32767) wetR = 32767;
    if (wetR < -32768) wetR = -32768;
    apStateR = static_cast<int16_t>(wetR);

    // Crossfade wet/dry mix: total gain never exceeds unity.
    // out = dry * (128 - depth) / 128 + wet * depth / 128
    int32_t dryL = left;
    int32_t dryR = right;
    int32_t dryAmt = 128 - static_cast<int32_t>(depth);
    int32_t mixL = (dryL * dryAmt + wetL * depth) >> 7;
    int32_t mixR = (dryR * dryAmt + wetR * depth) >> 7;
    if (mixL > 32767)  mixL = 32767;
    if (mixL < -32768) mixL = -32768;
    if (mixR > 32767)  mixR = 32767;
    if (mixR < -32768) mixR = -32768;
    left  = static_cast<int16_t>(mixL);
    right = static_cast<int16_t>(mixR);

    // Advance write position
    writeIdx = (writeIdx + 1) & CHORUS_BUF_MASK;
}

} // namespace synth
