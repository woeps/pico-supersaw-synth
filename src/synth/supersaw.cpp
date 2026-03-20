#include "synth/supersaw.h"
#include <cstring>
#include <cstdint>
#include <cstdio>

namespace synth {

// Precomputed phase increments for MIDI notes 0–127.
// phase_inc = freq / SAMPLE_RATE * 2^32
// freq = 440 * 2^((note - 69) / 12)
const uint32_t midiNotePhaseInc[128] = {
    796253, 843601, 893764, 946910,
    1003216, 1062871, 1126072, 1193032,
    1263973, 1339133, 1418762, 1503126,
    1592507, 1687202, 1787529, 1893821,
    2006433, 2125742, 2252145, 2386065,
    2527947, 2678267, 2837525, 3006253,
    3185014, 3374405, 3575058, 3787642,
    4012867, 4251484, 4504291, 4772130,
    5055895, 5356535, 5675051, 6012507,
    6370029, 6748811, 7150116, 7575284,
    8025734, 8502969, 9008582, 9544260,
    10111791, 10713070, 11350102, 12025014,
    12740059, 13497622, 14300233, 15150569,
    16051469, 17005939, 18017164, 19088521,
    20223583, 21426140, 22700205, 24050029,
    25480118, 26995245, 28600466, 30301138,
    32102938, 34011878, 36034329, 38177042,
    40447167, 42852281, 45400410, 48100059,
    50960237, 53990491, 57200933, 60602277,
    64205876, 68023756, 72068659, 76354085,
    80894335, 85704562, 90800821, 96200119,
    101920475, 107980982, 114401866, 121204555,
    128411752, 136047513, 144137319, 152708170,
    161788670, 171409125, 181601642, 192400238,
    203840951, 215961965, 228803732, 242409110,
    256823505, 272095026, 288274638, 305416340,
    323577341, 342818251, 363203285, 384800476,
    407681903, 431923931, 457607464, 484818220,
    513647011, 544190052, 576549277, 610832681,
    647154682, 685636502, 726406570, 769600953,
    815363807, 863847862, 915214929, 969636440,
    1027294023, 1088380105, 1153098554, 1221665362,
};

// Detune multipliers in Q16.16 fixed-point.
// Oscillator order: [-3/3, -2/3, -1/3, 0, +1/3, +2/3, +3/3] * 0.3 semitones
// multiplier = 2^(offset * 0.3 / 12), stored as (multiplier * 65536)
const uint32_t detuneMultiplier[NUM_OSCILLATORS] = {
    64410, 64783, 65159, 65536, 65916, 66297, 66682,
};

void Supersaw::init() {
    memset(phase, 0, sizeof(phase));
    memset(phaseInc, 0, sizeof(phaseInc));
    active = false;
    currentNote = 0;
    fadeState = FadeState::NONE;
    fadePos = 0;
}

void Supersaw::noteOn(uint8_t note, uint8_t velocity) {
    (void)velocity; // unused in PoC

    if (note > 127) return;

    currentNote = note;
    uint32_t baseInc = midiNotePhaseInc[note];

    // Apply detune multipliers (Q16.16 fixed-point multiply)
    for (int i = 0; i < NUM_OSCILLATORS; i++) {
        phaseInc[i] = static_cast<uint32_t>(
            (static_cast<uint64_t>(baseInc) * detuneMultiplier[i]) >> 16
        );
    }

    // Reset phases for a clean start
    memset(phase, 0, sizeof(phase));

    active = true;
    fadeState = FadeState::FADE_IN;
    fadePos = 0;
}

void Supersaw::noteOff() {
    if (!active) return;
    fadeState = FadeState::FADE_OUT;
    fadePos = 0;
}

void Supersaw::render(int16_t* buffer, size_t numStereoSamples) {
    for (size_t i = 0; i < numStereoSamples; i++) {
        int32_t sample = 0;

        if (active) {
            // Sum 7 sawtooth oscillators
            for (int osc = 0; osc < NUM_OSCILLATORS; osc++) {
                phase[osc] += phaseInc[osc];
                // Convert phase to sawtooth: map 0..2^32 to -32768..32767
                int32_t saw = static_cast<int32_t>(phase[osc] >> 16) - 32768;
                sample += saw;
            }

            // Normalize: divide by NUM_OSCILLATORS (7)
            // Use approximation: multiply by 9362 and shift right by 16
            // (9362/65536 ≈ 1/7 = 0.142857)
            sample = (sample * 9362) >> 16;

            // Apply fade ramp
            if (fadeState == FadeState::FADE_IN) {
                sample = (sample * static_cast<int32_t>(fadePos)) / FADE_SAMPLES;
                fadePos++;
                if (fadePos >= FADE_SAMPLES) {
                    fadeState = FadeState::NONE;
                }
            } else if (fadeState == FadeState::FADE_OUT) {
                sample = (sample * static_cast<int32_t>(FADE_SAMPLES - fadePos)) / FADE_SAMPLES;
                fadePos++;
                if (fadePos >= FADE_SAMPLES) {
                    fadeState = FadeState::NONE;
                    active = false;
                    sample = 0;
                }
            }
        }

        // Clamp to int16_t range
        if (sample > 32767) sample = 32767;
        if (sample < -32768) sample = -32768;

        // Write mono sample to both L and R channels (interleaved stereo)
        buffer[i * 2]     = static_cast<int16_t>(sample);
        buffer[i * 2 + 1] = static_cast<int16_t>(sample);
    }
}

} // namespace synth
