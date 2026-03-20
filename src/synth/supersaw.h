#ifndef SUPERSAW_H
#define SUPERSAW_H

#include <cstdint>
#include <cstddef>
#include "config/pins.h"

#define NUM_OSCILLATORS 7
#define FADE_SAMPLES 220 // ~5ms at 44100 Hz

namespace synth {

enum class FadeState : uint8_t {
    NONE,
    FADE_IN,
    FADE_OUT
};

struct Supersaw {
    uint32_t phase[NUM_OSCILLATORS];
    uint32_t phaseInc[NUM_OSCILLATORS];

    bool active;
    uint8_t currentNote;

    FadeState fadeState;
    uint32_t fadePos; // current sample position within the fade ramp

    void init();
    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff();
    void render(int16_t* buffer, size_t numStereoSamples);
};

// Precomputed phase increment table (one entry per MIDI note 0–127).
// phase_inc = freq / SAMPLE_RATE * 2^32
extern const uint32_t midiNotePhaseInc[128];

// Detune multipliers for the 7 oscillators, stored as Q16.16 fixed-point.
// Offsets: -3/3, -2/3, -1/3, 0, +1/3, +2/3, +3/3 of 0.3 semitones.
extern const uint32_t detuneMultiplier[NUM_OSCILLATORS];

} // namespace synth

#endif // SUPERSAW_H
