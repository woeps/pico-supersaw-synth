#ifndef PRESET_STORE_H
#define PRESET_STORE_H

#include <cstdint>
#include <cstddef>

namespace preset_store {

// Number of CC parameters stored in a preset.
#define PRESET_CC_COUNT 12

// Preset data layout stored in flash.
// Total: 4 (magic) + 1 (version) + 12 (CC values) = 17 bytes, padded to 256.
struct Preset {
    uint32_t magic;
    uint8_t  version;
    uint8_t  cc[PRESET_CC_COUNT];
};

static constexpr uint32_t PRESET_MAGIC   = 0x53415750; // "SAWP"
static constexpr uint8_t  PRESET_VERSION = 1;

// Index mapping: which CC number corresponds to each slot in cc[].
// Order: attack, decay, sustain, release, detune, spread, mix,
//        chorus_depth, chorus_rate, filter_cutoff, filter_reso, filter_mode.
extern const uint8_t presetCCMap[PRESET_CC_COUNT];

// Save a preset to the last sector of flash.
// IMPORTANT: This erases a 4 KB flash sector and programs a 256-byte page.
// Interrupts are disabled and Core 1 must be idle during this call.
// The caller is responsible for coordinating with Core 1.
void save(const Preset& preset);

// Load a preset from flash. Returns true if a valid preset was found.
bool load(Preset& preset);

} // namespace preset_store

#endif // PRESET_STORE_H
