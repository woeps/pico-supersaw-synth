#include "synth/supersaw.h"
#include "synth/saw_wavetable.h"
#include "config/midi_cc.h"
#include <cstring>
#include <cstdint>
#include <cstdio>
#include "pico/platform.h"

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

// JP-8000 asymmetric detune coefficients (Q16.16 offset from unity).
// Derived from JP firmware coefficients {−720, −412, −128, 0, +128, +408, +704}.
// Positive/negative pairs are intentionally unequal to prevent perfect
// cancellation and produce richer, less periodic beating.
static const int16_t detuneMaxOffset[NUM_OSCILLATORS] = {
    -10667, -6108, -1893, 0, +1893, +6048, +10430,
};

// JP-8000 piecewise-linear detune curve.
// Fine control at low CC values, exponential ramp at high values.
// CC 0-63: +1 every 2 steps, CC 64-80: +1/step, CC 81-120: +2/step,
// CC 121-123: +8/step, CC 124: +16, CC 125: +32, CC 126-127: +96/clamped.
static const uint8_t detuneCurve[128] = {
      1,   1,   2,   2,   3,   3,   4,   4,   5,   5,   6,   6,   7,   7,   8,   8,
      9,   9,  10,  10,  11,  11,  12,  12,  13,  13,  14,  14,  15,  15,  16,  16,
     17,  17,  18,  18,  19,  19,  20,  20,  21,  21,  22,  22,  23,  23,  24,  24,
     25,  25,  26,  26,  27,  27,  28,  28,  29,  29,  30,  30,  31,  31,  32,  32,
     33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,
     49,  51,  53,  55,  57,  59,  61,  63,  65,  67,  69,  71,  73,  75,  77,  79,
     81,  83,  85,  87,  89,  91,  93,  95,  97,  99, 101, 103, 105, 107, 109, 111,
    113, 115, 117, 119, 121, 123, 125, 127, 129, 137, 145, 153, 169, 201, 255, 255,
};

// Stereo pan offsets per oscillator (−3 … +3)
static const int8_t oscPanOffset[NUM_OSCILLATORS] = {-3, -2, -1, 0, 1, 2, 3};

// Envelope max level (Q16.16 stored in uint32_t)
static constexpr uint32_t ENV_MAX = 0xFFFF0000u;

// DC-blocking high-pass filter coefficient.
// α ≈ 0.9986 in Q14 → cutoff ~20 Hz @ 44.1 kHz.
// Q14: worst-case intermediate 16361 × 84000 = 1.37B, fits int32_t.
static constexpr int32_t HPF_ALPHA_Q14 = 16361;

// ─── Envelope ───────────────────────────────────────────────────────────────

void Envelope::gate(bool on) {
    if (on) {
        stage = EnvStage::ATTACK;
    } else {
        if (stage != EnvStage::IDLE) {
            stage = EnvStage::RELEASE;
        }
    }
}

uint32_t Envelope::tick(uint32_t attackInc, uint32_t decayInc,
                        uint32_t sustainLevel, uint32_t releaseInc) {
    switch (stage) {
        case EnvStage::ATTACK:
            if (level <= ENV_MAX - attackInc) {
                level += attackInc;
            } else {
                level = ENV_MAX;
                stage = EnvStage::DECAY;
            }
            break;
        case EnvStage::DECAY:
            if (level > sustainLevel && (level - sustainLevel) > decayInc) {
                level -= decayInc;
            } else {
                level = sustainLevel;
                stage = EnvStage::SUSTAIN;
            }
            break;
        case EnvStage::SUSTAIN:
            level = sustainLevel;
            break;
        case EnvStage::RELEASE:
            if (level > releaseInc) {
                level -= releaseInc;
            } else {
                level = 0;
                stage = EnvStage::IDLE;
            }
            break;
        case EnvStage::IDLE:
            level = 0;
            break;
    }
    return level;
}

// ─── Voice ──────────────────────────────────────────────────────────────────

void Voice::reset() {
    // Phase is intentionally NOT reset — free-running oscillators
    // produce organic variation between note triggers (JP-8000 behavior).
    memset(phaseInc, 0, sizeof(phaseInc));
    note = 0;
    active = false;
    age = 0;
    env.stage = EnvStage::IDLE;
    env.level = 0;
    hpfStateL = hpfStateR = hpfPrevL = hpfPrevR = 0;
    cachedTable = nullptr;
    cachedBandIdx = -1;
}

// ─── Supersaw ───────────────────────────────────────────────────────────────

uint32_t Supersaw::ccToEnvInc(uint8_t cc) {
    // Quadratic mapping: CC 0 → ~2 ms, CC 127 → ~2000 ms
    uint32_t c = cc;
    uint32_t time_ms = 2 + (c * c * 1998) / 16129;
    uint32_t samples = (static_cast<uint32_t>(SAMPLE_RATE) * time_ms) / 1000;
    if (samples < 1) samples = 1;
    return ENV_MAX / samples;
}

void Supersaw::init() {
    for (int i = 0; i < MAX_VOICES; i++) {
        voices[i].reset();
    }
    nextAge = 0;

    // Default ADSR: near-instant attack, no decay, full sustain, short release
    attackInc   = ccToEnvInc(0);    // ~2 ms
    decayInc    = ccToEnvInc(0);    // ~2 ms
    sustainLevel = ENV_MAX;          // full sustain
    releaseInc  = ccToEnvInc(10);   // ~15 ms

    detuneAmount = detuneCurve[76];  // CC 76 through JP curve
    spread = 0;         // mono (matches PoC)
    mixAmount = 127;    // full supersaw (all side oscillators at max)

    // Initialize parameter smoothing
    targetMix = currentMix = 256;  // Q8.8: 256 = full side gain
    targetDetune = currentDetune = static_cast<int32_t>(detuneAmount) << 8;
    detuneSmoothCounter = 0;

    recalcSpread();

    // Initialize wavetable SRAM band cache
    for (int i = 0; i < MAX_CACHED_BANDS; i++) {
        bandCache[i].bandIdx = -1;
        bandCache[i].refCount = 0;
    }

    chorus.init();
    filter.init();

    // Initialize dual-core rendering synchronization
    core1RenderCmd = 0;
    core1RenderDone = false;
    cacheLock = spin_lock_init(spin_lock_claim_unused(true));
}

const int16_t* Supersaw::cacheAcquire(uint8_t bandIdx) {
    uint32_t save = spin_lock_blocking(cacheLock);
    // Check if this band is already cached
    for (int i = 0; i < MAX_CACHED_BANDS; i++) {
        if (bandCache[i].bandIdx == static_cast<int8_t>(bandIdx)) {
            bandCache[i].refCount++;
            spin_unlock(cacheLock, save);
            return bandCache[i].data;
        }
    }
    // Find an empty slot and copy from flash
    for (int i = 0; i < MAX_CACHED_BANDS; i++) {
        if (bandCache[i].bandIdx < 0) {
            memcpy(bandCache[i].data, sawWavetable[bandIdx], WAVETABLE_SIZE * sizeof(int16_t));
            bandCache[i].bandIdx = static_cast<int8_t>(bandIdx);
            bandCache[i].refCount = 1;
            spin_unlock(cacheLock, save);
            return bandCache[i].data;
        }
    }
    // Cache full — fall back to flash
    spin_unlock(cacheLock, save);
    return nullptr;
}

void Supersaw::cacheRelease(int8_t bandIdx) {
    if (bandIdx < 0) return;
    uint32_t save = spin_lock_blocking(cacheLock);
    for (int i = 0; i < MAX_CACHED_BANDS; i++) {
        if (bandCache[i].bandIdx == bandIdx) {
            if (bandCache[i].refCount > 0) {
                bandCache[i].refCount--;
            }
            if (bandCache[i].refCount == 0) {
                bandCache[i].bandIdx = -1;
            }
            spin_unlock(cacheLock, save);
            return;
        }
    }
    spin_unlock(cacheLock, save);
}

void Supersaw::noteOn(uint8_t note, uint8_t velocity) {
    (void)velocity;
    if (note > 127) return;

    // Retrigger if this note is already active
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active && voices[i].note == note) {
            voices[i].age = nextAge++;
            voices[i].env.gate(true);
            updateDetuneForVoice(voices[i]);
            return;
        }
    }

    // Find a free voice, or steal the oldest
    int idx = 0;
    uint32_t oldestAge = UINT32_MAX;
    bool found = false;

    for (int i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active) {
            idx = i;
            found = true;
            break;
        }
        if (voices[i].age < oldestAge) {
            oldestAge = voices[i].age;
            idx = i;
        }
    }

    (void)found;
    // Release cache for stolen voice before reset
    cacheRelease(voices[idx].cachedBandIdx);
    voices[idx].reset();
    voices[idx].note = note;
    voices[idx].active = true;
    voices[idx].age = nextAge++;
    voices[idx].env.gate(true);
    updateDetuneForVoice(voices[idx]);

    // Acquire SRAM-cached wavetable band for high notes
    if (note >= 72) {
        uint8_t band = noteToOctaveBand[note];
        voices[idx].cachedTable = cacheAcquire(band);
        voices[idx].cachedBandIdx = static_cast<int8_t>(band);
    }
}

void Supersaw::noteOff(uint8_t note) {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active && voices[i].note == note) {
            voices[i].env.gate(false);
            break;
        }
    }
}

void Supersaw::setCC(uint8_t cc, uint8_t value) {
    if (cc == CC_ATTACK) {
        attackInc = ccToEnvInc(value);
    } else if (cc == CC_DECAY) {
        decayInc = ccToEnvInc(value);
    } else if (cc == CC_SUSTAIN) {
        sustainLevel = (static_cast<uint32_t>(value) * 65535u / 127u) << 16;
    } else if (cc == CC_RELEASE) {
        releaseInc = ccToEnvInc(value);
    } else if (cc == CC_DETUNE) {
        targetDetune = static_cast<int32_t>(detuneCurve[value]) << 8;
    } else if (cc == CC_SPREAD) {
        spread = value;
        recalcSpread();
    } else if (cc == CC_MIX) {
        targetMix = (static_cast<int32_t>(value) * 256) / 127;
    } else if (cc == CC_CHORUS_DEPTH) {
        chorus.setDepth(value);
    } else if (cc == CC_CHORUS_RATE) {
        chorus.setRate(value);
    } else if (cc == CC_FILTER_CUTOFF) {
        filter.setCutoff(value);
    } else if (cc == CC_FILTER_RESO) {
        filter.setResonance(value);
    } else if (cc == CC_FILTER_MODE) {
        filter.setMode(value);
    }
}

void Supersaw::updateDetuneForVoice(Voice& v) {
    uint32_t baseInc = midiNotePhaseInc[v.note];
    for (int i = 0; i < NUM_OSCILLATORS; i++) {
        // Interpolate between unity (65536) and max-detune multiplier
        int32_t offset = (static_cast<int32_t>(detuneMaxOffset[i]) * detuneAmount) / 255;
        uint32_t multiplier = static_cast<uint32_t>(65536 + offset);
        v.phaseInc[i] = static_cast<uint32_t>(
            (static_cast<uint64_t>(baseInc) * multiplier) >> 16
        );
    }
}

void Supersaw::recalcSpread() {
    // At spread=0: panL = panR = 256 (unity, mono).
    // At spread=127: outermost oscillators hard-panned (panL=384/panR=128 or vice versa).
    for (int i = 0; i < NUM_OSCILLATORS; i++) {
        int32_t pan = (static_cast<int32_t>(oscPanOffset[i]) * spread * 128) / (3 * 127);
        panL[i] = static_cast<uint16_t>(256 - pan);
        panR[i] = static_cast<uint16_t>(256 + pan);
    }
}

bool Supersaw::anyVoiceActive() const {
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active) return true;
    }
    return false;
}

void Supersaw::renderVoiceSample(int startV, int endV,
                                  int32_t& outL, int32_t& outR,
                                  int32_t sideGain) {
    static constexpr int32_t CENTER_GAIN = 50; // 50/256 ≈ 0.195 in Q8.8

    for (int v = startV; v < endV; v++) {
        Voice& voice = voices[v];
        if (!voice.active) continue;

        uint32_t envLevel = voice.env.tick(attackInc, decayInc,
                                            sustainLevel, releaseInc);
        if (voice.env.stage == EnvStage::IDLE) {
            cacheRelease(voice.cachedBandIdx);
            voice.active = false;
            voice.cachedTable = nullptr;
            voice.cachedBandIdx = -1;
            continue;
        }

        int32_t voiceL = 0;
        int32_t voiceR = 0;

        for (int osc = 0; osc < NUM_OSCILLATORS; osc++) {
            voice.phase[osc] += voice.phaseInc[osc];

            int32_t saw;
            if (voice.note < 72) {
                // Naive saw: raw phase accumulator for JP-8000 aliasing character.
                saw = static_cast<int32_t>(voice.phase[osc] >> 17) - 16384;
            } else {
                // Band-limited wavetable for high notes to avoid harsh aliasing
                const int16_t* table = voice.cachedTable
                    ? voice.cachedTable
                    : sawWavetable[noteToOctaveBand[voice.note]];
                uint32_t idx = voice.phase[osc] >> 21;
                uint32_t frac = (voice.phase[osc] >> 5) & 0xFFFF;
                int16_t s0 = table[idx];
                int16_t s1 = table[(idx + 1) & (WAVETABLE_SIZE - 1)];
                saw = s0 + ((static_cast<int32_t>(s1 - s0) * static_cast<int32_t>(frac)) >> 16);
            }

            int32_t gain = (osc == 3) ? CENTER_GAIN : sideGain;
            voiceL += (saw * gain * static_cast<int32_t>(panL[osc])) >> 16;
            voiceR += (saw * gain * static_cast<int32_t>(panR[osc])) >> 16;
        }

        // Normalize by max effective gain (mix=127):
        // center=50/256 + 6×sides=256/256 → ~6.195× saw amplitude.
        // ÷6.195 ≈ ×10579 >> 16.
        voiceL = (voiceL * 10579) >> 16;
        voiceR = (voiceR * 10579) >> 16;

        // DC-blocking high-pass filter (removes DC offset and sub-fundamental energy)
        voice.hpfStateL = (HPF_ALPHA_Q14 * (voice.hpfStateL + voiceL - voice.hpfPrevL)) >> 14;
        voice.hpfPrevL = voiceL;
        voiceL = voice.hpfStateL;

        voice.hpfStateR = (HPF_ALPHA_Q14 * (voice.hpfStateR + voiceR - voice.hpfPrevR)) >> 14;
        voice.hpfPrevR = voiceR;
        voiceR = voice.hpfStateR;

        // Apply envelope: envLevel is 0..0xFFFF0000, use upper 16 bits as multiplier
        int32_t envMul = static_cast<int32_t>(envLevel >> 16);
        voiceL = (voiceL * envMul) >> 16;
        voiceR = (voiceR * envMul) >> 16;

        outL += voiceL;
        outR += voiceR;
    }
}

void Supersaw::render(int16_t* buffer, size_t numStereoSamples) {
    // Signal Core 1 to render voices 2–3 into scratch buffer
    core1RenderDone = false;
    core1RenderCmd = numStereoSamples;

    // Core 0: render voices 0–1 into output buffer (temporary partial mix)
    for (size_t i = 0; i < numStereoSamples; i++) {
        int32_t sampleL = 0;
        int32_t sampleR = 0;

        // Parameter smoothing: slew mix and detune toward targets
        currentMix += (targetMix - currentMix) >> 6;
        currentDetune += (targetDetune - currentDetune) >> 6;

        // Periodically recalculate phase increments from smoothed detune
        // (Core 0 updates voices 0–1 only; Core 1 updates voices 2–3)
        if (++detuneSmoothCounter >= 32) {
            detuneSmoothCounter = 0;
            detuneAmount = static_cast<uint8_t>(currentDetune >> 8);
            for (int v = 0; v < 2; v++) {
                if (voices[v].active) updateDetuneForVoice(voices[v]);
            }
        }

        renderVoiceSample(0, 2, sampleL, sampleR, currentMix);

        // Clamp partial 2-voice sum to int16_t for temporary storage
        if (sampleL > 32767) sampleL = 32767;
        if (sampleL < -32768) sampleL = -32768;
        if (sampleR > 32767) sampleR = 32767;
        if (sampleR < -32768) sampleR = -32768;

        buffer[i * 2]     = static_cast<int16_t>(sampleL);
        buffer[i * 2 + 1] = static_cast<int16_t>(sampleR);
    }

    // Wait for Core 1 to complete rendering voices 2–3
    while (!core1RenderDone) {
        tight_loop_contents();
    }

    // Merge Core 1's scratch buffer, normalize, clamp, and apply chorus
    for (size_t i = 0; i < numStereoSamples; i++) {
        int32_t sampleL = static_cast<int32_t>(buffer[i * 2])     + core1ScratchBuf[i * 2];
        int32_t sampleR = static_cast<int32_t>(buffer[i * 2 + 1]) + core1ScratchBuf[i * 2 + 1];

        // Divide by MAX_VOICES to prevent clipping with full polyphony
        sampleL >>= 2;
        sampleR >>= 2;

        // Clamp to int16_t range
        if (sampleL > 32767) sampleL = 32767;
        if (sampleL < -32768) sampleL = -32768;
        if (sampleR > 32767) sampleR = 32767;
        if (sampleR < -32768) sampleR = -32768;

        int16_t outL = static_cast<int16_t>(sampleL);
        int16_t outR = static_cast<int16_t>(sampleR);

        // SVF filter (post-mix, pre-chorus)
        filter.process(outL, outR);

        // Stereo chorus (post-filter, pre-output)
        chorus.process(outL, outR);

        buffer[i * 2]     = outL;
        buffer[i * 2 + 1] = outR;
    }
}

void Supersaw::renderCore1Voices(size_t numStereoSamples) {
    uint8_t localDetuneCounter = 0;

    for (size_t i = 0; i < numStereoSamples; i++) {
        int32_t sampleL = 0;
        int32_t sampleR = 0;

        // Periodically recalculate detune for Core 1's voices (2–3)
        if (++localDetuneCounter >= 32) {
            localDetuneCounter = 0;
            for (int v = 2; v < MAX_VOICES; v++) {
                if (voices[v].active) updateDetuneForVoice(voices[v]);
            }
        }

        renderVoiceSample(2, MAX_VOICES, sampleL, sampleR, currentMix);

        core1ScratchBuf[i * 2]     = sampleL;
        core1ScratchBuf[i * 2 + 1] = sampleR;
    }
}

} // namespace synth
