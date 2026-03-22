#ifndef SUPERSAW_H
#define SUPERSAW_H

#include <cstdint>
#include <cstddef>
#include "hardware/sync.h"
#include "config/pins.h"
#include "audio/audio_output.h"
#include "synth/chorus.h"
#include "synth/filter.h"
#include "synth/saw_wavetable.h"

#define NUM_OSCILLATORS 7
#define MAX_VOICES 4
#define MAX_CACHED_BANDS 4

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

    // Pointer to SRAM-cached wavetable band (nullptr if note < 72 or uncached)
    const int16_t* cachedTable;
    int8_t cachedBandIdx;  // which band is cached, or -1 if none

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

    // Raw CC values for preset save/restore (indexed by CC number, 128 slots).
    uint8_t rawCC[128];

    StereoChorus chorus;
    SVFilter filter;

    // Dual-core voice rendering: Core 1 writes its partial mix here.
    // Sized for max buffer (stereo pairs as int32_t to avoid overflow from 2-voice sum).
    int32_t core1ScratchBuf[AUDIO_BUFFER_SAMPLES * 2];

    // Inter-core synchronization (volatile for cross-core visibility).
    // core1RenderCmd: 0 = idle, >0 = number of stereo samples to render.
    // core1RenderDone: set true by Core 1 when rendering is complete.
    volatile uint32_t core1RenderCmd;
    volatile bool core1RenderDone;

    // Hardware spinlock for bandCache access (shared between cores).
    spin_lock_t* cacheLock;

    // SRAM wavetable band cache: shared across voices with reference counting.
    // Each slot holds a copy of one octave band (WAVETABLE_SIZE × int16_t = 4 KB).
    struct BandCacheEntry {
        int16_t data[WAVETABLE_SIZE];
        int8_t bandIdx;   // octave band index stored here, or -1 if empty
        uint8_t refCount; // number of voices referencing this entry
    };
    BandCacheEntry bandCache[MAX_CACHED_BANDS];

    void init();
    void noteOn(uint8_t note, uint8_t velocity);
    void noteOff(uint8_t note);
    void setCC(uint8_t cc, uint8_t value);
    void render(int16_t* buffer, size_t numStereoSamples);

    // Called by Core 1: renders voices 2–3 into core1ScratchBuf.
    void renderCore1Voices(size_t numStereoSamples);

    bool anyVoiceActive() const;

    // Return the raw CC value last set for a given CC number.
    uint8_t getCC(uint8_t cc) const;

private:
    void updateDetuneForVoice(Voice& v);
    void recalcSpread();
    static uint32_t ccToEnvInc(uint8_t cc);
    const int16_t* cacheAcquire(uint8_t bandIdx);
    void cacheRelease(int8_t bandIdx);

    // Render a range of voices [startV, endV) for one sample.
    // Accumulates into outL/outR. Handles envelope tick, HPF, and cache release on IDLE.
    void renderVoiceSample(int startV, int endV,
                           int32_t& outL, int32_t& outR,
                           int32_t sideGain);
};

// Precomputed phase increment table (one entry per MIDI note 0–127).
// phase_inc = freq / SAMPLE_RATE * 2^32
extern const uint32_t midiNotePhaseInc[128];

} // namespace synth

#endif // SUPERSAW_H
