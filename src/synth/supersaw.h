#ifndef SUPERSAW_H
#define SUPERSAW_H

#include <cstdint>
#include <cstddef>
#include "config/pins.h"
#include "synth/chorus.h"

#define NUM_OSCILLATORS 7
#define MAX_VOICES 4

namespace synth {

// ADSR envelope state machine (per-voice)
enum class EnvStage : uint8_t {
    IDLE,
    ATTACK,
    DECAY,
    SUSTAIN,
    RELEASE
};

struct Envelope {
    EnvStage stage;
    uint32_t level;    // current level, Q16.16 (0 = silent, 65536 = full)

    void gate(bool on);
    // Advance one sample; returns current level (Q16.16).
    // Shared ADSR rates are passed in to avoid per-voice storage.
    uint32_t tick(uint32_t attackInc, uint32_t decayInc,
                  uint32_t sustainLevel, uint32_t releaseInc);
};

struct Voice {
    uint32_t phase[NUM_OSCILLATORS];
    uint32_t phaseInc[NUM_OSCILLATORS];
    uint8_t  note;
    bool     active;     // true while voice is sounding (including release)
    uint32_t age;        // incremented each noteOn for voice-stealing
    Envelope env;

    // DC-blocking high-pass filter state (per channel)
    int32_t hpfStateL;
    int32_t hpfStateR;
    int32_t hpfPrevL;
    int32_t hpfPrevR;

    void reset();
};

struct Supersaw {
    Voice voices[MAX_VOICES];
    uint32_t nextAge; // monotonic counter for voice allocation

    // Shared ADSR parameters (Q16.16 increments / level)
    uint32_t attackInc;    // level increment per sample during attack
    uint32_t decayInc;     // level decrement per sample during decay
    uint32_t sustainLevel; // sustain target level (Q16.16)
    uint32_t releaseInc;   // level decrement per sample during release

    uint8_t detuneAmount;  // 0–255 (mapped through detuneCurve)
    uint8_t spread;        // 0–127 (CC value)
    uint8_t mixAmount;     // 0–127 (CC value), 0 = center only, 127 = full supersaw

    // Parameter smoothing (one-pole slew to prevent zipper noise)
    int32_t currentMix;        // smoothed side gain, Q8.8 (0–256)
    int32_t targetMix;         // target side gain, Q8.8
    int32_t currentDetune;     // smoothed detune amount, Q8.8
    int32_t targetDetune;      // target detune amount, Q8.8
    uint8_t detuneSmoothCounter; // sample counter for periodic detune recalc

    // Per-oscillator stereo pan gains, derived from spread.
    // Q8.8 fixed-point: 256 = unity.
    uint16_t panL[NUM_OSCILLATORS];
    uint16_t panR[NUM_OSCILLATORS];

    StereoChorus chorus;

    void init();
    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);
    void setCC(uint8_t cc, uint8_t value);
    void render(int16_t* buffer, size_t numStereoSamples);

    bool anyVoiceActive() const;

private:
    void updateDetuneForVoice(Voice& v);
    void recalcSpread();
    static uint32_t ccToEnvInc(uint8_t cc);
};

// Precomputed phase increment table (one entry per MIDI note 0–127).
// phase_inc = freq / SAMPLE_RATE * 2^32
extern const uint32_t midiNotePhaseInc[128];

} // namespace synth

#endif // SUPERSAW_H
