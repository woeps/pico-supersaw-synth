#ifndef FILTER_H
#define FILTER_H

#include <cstdint>
#include "synth/filter_cutoff_table.h"

namespace synth {

// SVF filter mode
enum class FilterMode : uint8_t {
    LPF = 0,
    HPF = 1,
    BPF = 2
};

// State-variable clamp: ±2× the Q28 input level, giving the same
// headroom ratio as the original ±32767 clamp at Q14.
static constexpr int32_t STATE_MAX = (1 << 28) - 1;

struct SVFilter {
    int32_t s1_L, s2_L;    // Left channel state  (Q28)
    int32_t s1_R, s2_R;    // Right channel state (Q28)
    int32_t cutoffCoeff;       // Q14, from lookup table (smoothed)
    int32_t targetCutoffCoeff; // Q14, smoothing target set by setCutoff()
    int32_t dampCoeff;     // Q14, derived from resonance CC
    int32_t D;             // Denominator: 1 + 2Rg + g^2 (Q14)
    int32_t invD;          // Reciprocal: round(2^28 / D) (Q14)
    int32_t resoCompGain;  // Q14 resonance gain compensation: min(16384, dampCoeff)
    FilterMode mode;

    void init();
    void setCutoff(uint8_t cc);
    void setResonance(uint8_t cc);
    void setMode(uint8_t cc);

    // Slew cutoffCoeff toward targetCutoffCoeff and recompute D/invD.
    // Called periodically (off the per-sample path) to avoid zipper noise
    // when the cutoff CC changes abruptly.
    void tickSmoothing();

    // Process one stereo sample pair in-place (double-sampled SVF).
    void process(int16_t& left, int16_t& right);
};

} // namespace synth

#endif // FILTER_H
