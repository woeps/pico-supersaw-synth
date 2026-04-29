#include "synth/supersaw.h"
#include "synth/saw_wavetable.h"
#include "config/midi_cc.h"
#include <cstring>
#include <cstdint>
#include <cstdio>
#include "pico/platform.h"

namespace synth {

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
            if (level == 0) {
                stage = EnvStage::IDLE;
                break;
            }
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
    velocity = 0;
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

    // Initialize raw CC tracking to defaults
    memset(rawCC, 0, sizeof(rawCC));
    rawCC[CC_ATTACK]  = 0;
    rawCC[CC_DECAY]   = 0;
    rawCC[CC_SUSTAIN] = 127;
    rawCC[CC_RELEASE] = 10;
    rawCC[CC_DETUNE]  = 76;
    rawCC[CC_SPREAD]  = 0;
    rawCC[CC_MIX]     = 127;
    rawCC[CC_CHORUS_DEPTH] = 0;
    rawCC[CC_CHORUS_RATE]  = 0;
    rawCC[CC_FILTER_CUTOFF] = 127;
    rawCC[CC_FILTER_RESO]   = 0;
    rawCC[CC_FILTER_MODE]   = 0;

    // Initialize parameter smoothing
    targetMix = currentMix = 65536;  // Q16.16: 65536 = full side gain (256 in Q8.8)
    targetDetune = currentDetune = static_cast<int32_t>(detuneAmount) << 16;
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
    core1RenderDone = true;

    dbgClipCount = 0;
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
    if (note > 127) return;

    // Retrigger if this note is already active
    for (int i = 0; i < MAX_VOICES; i++) {
        if (voices[i].active && voices[i].note == note) {
            voices[i].age = nextAge++;
            voices[i].velocity = velocity;
            voices[i].env.gate(true);
            updateDetuneForVoice(voices[i]);
            return;
        }
    }

    // Find a free voice, or steal: prefer RELEASE voices (lowest level), then oldest
    int idx = -1;
    int releaseIdx = -1;
    int oldestIdx = -1;
    uint32_t lowestReleaseLevel = UINT32_MAX;
    uint32_t oldestAge = UINT32_MAX;

    for (int i = 0; i < MAX_VOICES; i++) {
        if (!voices[i].active) {
            idx = i;
            break;
        }
        if (voices[i].env.stage == EnvStage::RELEASE &&
            voices[i].env.level < lowestReleaseLevel) {
            lowestReleaseLevel = voices[i].env.level;
            releaseIdx = i;
        }
        if (voices[i].age < oldestAge) {
            oldestAge = voices[i].age;
            oldestIdx = i;
        }
    }
    if (idx == -1) {
        // Prefer stealing a RELEASE voice; fall back to oldest active voice
        if (releaseIdx != -1) {
            idx = releaseIdx;
        } else if (oldestIdx != -1) {
            idx = oldestIdx;
        } else {
            idx = 0; // safety fallback
        }
    }

    // cacheRelease acquires cacheLock internally.
    cacheRelease(voices[idx].cachedBandIdx);

    voices[idx].reset();
    voices[idx].note = note;
    voices[idx].velocity = velocity;
    voices[idx].active = true;
    voices[idx].age = nextAge++;
    voices[idx].env.gate(true);
    updateDetuneForVoice(voices[idx]);

    // Acquire SRAM-cached wavetable band for high notes (acquires cacheLock internally).
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
        }
    }
}

void Supersaw::setCC(uint8_t cc, uint8_t value) {
    rawCC[cc] = value;
    if (cc == CC_ATTACK) {
        attackInc = ccToEnvInc(value);
    } else if (cc == CC_DECAY) {
        decayInc = ccToEnvInc(value);
    } else if (cc == CC_SUSTAIN) {
        sustainLevel = (static_cast<uint32_t>(value) * 65535u / 127u) << 16;
    } else if (cc == CC_RELEASE) {
        releaseInc = ccToEnvInc(value);
    } else if (cc == CC_DETUNE) {
        targetDetune = static_cast<int32_t>(detuneCurve[value]) << 16;
    } else if (cc == CC_SPREAD) {
        spread = value;
        recalcSpread();
    } else if (cc == CC_MIX) {
        targetMix = ((static_cast<int32_t>(value) * 256) / 127) << 8;
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

        // Apply logarithmic velocity gain (Q8: 256 = unity)
        int32_t velGain = static_cast<int32_t>(velocityGainTable[voice.velocity]);
        voiceL = (voiceL * velGain) >> 8;
        voiceR = (voiceR * velGain) >> 8;

        outL += voiceL;
        outR += voiceR;
    }
}

void Supersaw::render(int16_t* buffer, size_t numStereoSamples) {
    // Signal Core 1 to render voices 2–3 into scratch buffer
    core1RenderCmd = numStereoSamples;
    __dmb(); // Ensure cmd is visible before triggering
    core1RenderDone = false; // Trigger Core 1

    // Core 0: render voices 0–1 into int32_t scratch buffer (no premature clamp)
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
            detuneAmount = static_cast<uint8_t>(currentDetune >> 16);
            for (int v = 0; v < 2; v++) {
                if (voices[v].active) updateDetuneForVoice(voices[v]);
            }
        }

        renderVoiceSample(0, 2, sampleL, sampleR, currentMix >> 8);

        core0ScratchBuf[i * 2]     = sampleL;
        core0ScratchBuf[i * 2 + 1] = sampleR;
    }

    // Wait for Core 1 to complete rendering voices 2–3
    while (!core1RenderDone) {
        tight_loop_contents();
    }
    __dmb(); // Ensure Core 1's scratch buffer writes are visible to Core 0

    // Merge both int32_t scratch buffers, normalize, clamp, and apply chorus
    uint32_t localClips = 0;
    for (size_t i = 0; i < numStereoSamples; i++) {
        int32_t sampleL = core0ScratchBuf[i * 2]     + core1ScratchBuf[i * 2];
        int32_t sampleR = core0ScratchBuf[i * 2 + 1] + core1ScratchBuf[i * 2 + 1];

        // Divide by 2: each voice is already normalized to ~16384 (half of
        // int16_t range) by the ×10579>>16 factor in renderVoiceSample, so
        // four voices sum to ~65536 and only a ÷2 is needed to fit int16_t.
        sampleL >>= 1;
        sampleR >>= 1;

        // Soft limiter: 2:1 compression above 75% of full scale.
        // Preserves full dynamics for 1–3 voices; gently tames 4-voice
        // peaks so the filter and DAC have ~3 dB of headroom.
        // Max output: 24576 + (32768−24576)/2 = 28672 (87.5%).
        // Cost: 2 compares + 1 sub + 1 shift per channel.
        static constexpr int32_t SOFT_THRESH = 24576; // 75% of 32767
        if (sampleL > SOFT_THRESH) {
            sampleL = SOFT_THRESH + ((sampleL - SOFT_THRESH) >> 1);
        } else if (sampleL < -SOFT_THRESH) {
            sampleL = -SOFT_THRESH + ((sampleL + SOFT_THRESH) >> 1);
        }
        if (sampleR > SOFT_THRESH) {
            sampleR = SOFT_THRESH + ((sampleR - SOFT_THRESH) >> 1);
        } else if (sampleR < -SOFT_THRESH) {
            sampleR = -SOFT_THRESH + ((sampleR + SOFT_THRESH) >> 1);
        }

        // Clamp to int16_t range (safety net — soft limiter caps at 28672)
        if (sampleL > 32767) { sampleL = 32767; localClips++; }
        if (sampleL < -32768) { sampleL = -32768; localClips++; }
        if (sampleR > 32767) { sampleR = 32767; localClips++; }
        if (sampleR < -32768) { sampleR = -32768; localClips++; }

        int16_t outL = static_cast<int16_t>(sampleL);
        int16_t outR = static_cast<int16_t>(sampleR);

        // SVF filter (post-mix, pre-chorus)
        filter.process(outL, outR);

        // Stereo chorus (post-filter, pre-output)
        chorus.process(outL, outR);

        buffer[i * 2]     = outL;
        buffer[i * 2 + 1] = outR;
    }
    dbgClipCount += localClips;
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

        renderVoiceSample(2, MAX_VOICES, sampleL, sampleR, currentMix >> 8);

        core1ScratchBuf[i * 2]     = sampleL;
        core1ScratchBuf[i * 2 + 1] = sampleR;
    }
}

uint8_t Supersaw::getCC(uint8_t cc) const {
    return rawCC[cc];
}

} // namespace synth
