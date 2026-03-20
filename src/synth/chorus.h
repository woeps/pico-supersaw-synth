#ifndef CHORUS_H
#define CHORUS_H

#include <cstdint>
#include <cstddef>

namespace synth {

// Stereo chorus delay buffer size: 15 ms @ 44100 Hz = 662 samples.
// Rounded up to next power-of-two for efficient wrapping via bitmask.
#define CHORUS_BUF_SIZE 1024
#define CHORUS_BUF_MASK (CHORUS_BUF_SIZE - 1)

// Pre-computed triangle LFO lookup table (256 entries, stored in flash).
// Values range from 0 to 32767 (Q15 unsigned triangle wave).
extern const int16_t chorusLfoTable[256];

struct StereoChorus {
    int16_t delayBufL[CHORUS_BUF_SIZE];
    int16_t delayBufR[CHORUS_BUF_SIZE];
    uint16_t writeIdx;

    // LFO state: 32-bit phase accumulator, upper 8 bits index the table.
    uint32_t lfoPhase;
    uint32_t lfoInc;   // phase increment per sample (controls rate)

    uint8_t depth;     // wet/dry mix, 0–127 (CC 91)
    uint8_t rate;      // LFO rate, 0–127 (CC 92)

    void init();
    void setDepth(uint8_t value);
    void setRate(uint8_t value);

    // Process one stereo sample pair in-place.
    void process(int16_t& left, int16_t& right);
};

} // namespace synth

#endif // CHORUS_H
