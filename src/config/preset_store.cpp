#include "config/preset_store.h"
#include "config/midi_cc.h"
#include <cstring>
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"

namespace preset_store {

// CC number for each slot in Preset::cc[].
const uint8_t presetCCMap[PRESET_CC_COUNT] = {
    CC_ATTACK,
    CC_DECAY,
    CC_SUSTAIN,
    CC_RELEASE,
    CC_DETUNE,
    CC_SPREAD,
    CC_MIX,
    CC_CHORUS_DEPTH,
    CC_CHORUS_RATE,
    CC_FILTER_CUTOFF,
    CC_FILTER_RESO,
    CC_FILTER_MODE,
};

// Use the last sector of flash for preset storage.
// PICO_FLASH_SIZE_BYTES is defined by the SDK based on PICO_BOARD.
static constexpr uint32_t FLASH_PRESET_OFFSET =
    PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;

// XiP base address for reading flash as memory.
static const uint8_t* FLASH_PRESET_ADDR =
    reinterpret_cast<const uint8_t*>(XIP_BASE + FLASH_PRESET_OFFSET);

void save(const Preset& preset) {
    // Prepare a 256-byte page buffer (minimum flash write size).
    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &preset, sizeof(Preset));

    // Disable interrupts — flash is inaccessible during erase/write.
    multicore_lockout_start_blocking();
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(FLASH_PRESET_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(FLASH_PRESET_OFFSET, page, FLASH_PAGE_SIZE);
    restore_interrupts(ints);
    multicore_lockout_end_blocking();
}

bool load(Preset& preset) {
    // Read directly from XiP-mapped flash.
    memcpy(&preset, FLASH_PRESET_ADDR, sizeof(Preset));

    if (preset.magic != PRESET_MAGIC || preset.version != PRESET_VERSION) {
        return false;
    }

    // Sanity-check: all CC values must be 0–127.
    for (int i = 0; i < PRESET_CC_COUNT; i++) {
        if (preset.cc[i] > 127) {
            return false;
        }
    }
    return true;
}

} // namespace preset_store
