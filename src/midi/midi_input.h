#ifndef MIDI_INPUT_H
#define MIDI_INPUT_H

#include <cstdint>
#include "hardware/uart.h"

namespace midi {

enum class MidiEventType : uint8_t {
    NOTE_ON  = 0,
    NOTE_OFF = 1,
    CC       = 2,
    PANIC    = 3,
};

// MIDI event packed into 32 bits for multicore FIFO.
// Bit layout: [23:20] channel, [17:16] type, [14:8] param1, [6:0] param2
struct MidiEvent {
    MidiEventType type;
    uint8_t channel; // MIDI channel 0–15 (Omni mode ignores this for now)
    uint8_t param1;  // note number or CC number (0–127)
    uint8_t param2;  // velocity or CC value     (0–127)

    // Packed-word field positions. Shifts give the bit offset of each field;
    // masks are applied to the raw (unshifted) value before/after shifting.
    static constexpr uint32_t CHANNEL_SHIFT = 20;
    static constexpr uint32_t CHANNEL_MASK  = 0x0F; // 4 bits: channel 0–15
    static constexpr uint32_t TYPE_SHIFT    = 16;
    static constexpr uint32_t TYPE_MASK     = 0x03; // 2 bits: MidiEventType
    static constexpr uint32_t PARAM1_SHIFT  = 8;
    static constexpr uint32_t PARAM1_MASK   = 0x7F; // 7 bits: 0–127
    static constexpr uint32_t PARAM2_SHIFT  = 0;
    static constexpr uint32_t PARAM2_MASK   = 0x7F; // 7 bits: 0–127

    uint32_t pack() const {
        return (static_cast<uint32_t>(channel & CHANNEL_MASK) << CHANNEL_SHIFT) |
               (static_cast<uint32_t>(type) << TYPE_SHIFT) |
               (static_cast<uint32_t>(param1 & PARAM1_MASK) << PARAM1_SHIFT) |
               (static_cast<uint32_t>(param2 & PARAM2_MASK) << PARAM2_SHIFT);
    }

    static MidiEvent unpack(uint32_t data) {
        MidiEvent e;
        e.channel = static_cast<uint8_t>((data >> CHANNEL_SHIFT) & CHANNEL_MASK);
        e.type    = static_cast<MidiEventType>((data >> TYPE_SHIFT) & TYPE_MASK);
        e.param1  = static_cast<uint8_t>((data >> PARAM1_SHIFT) & PARAM1_MASK);
        e.param2  = static_cast<uint8_t>((data >> PARAM2_SHIFT) & PARAM2_MASK);
        return e;
    }
};

bool midiEventAvailable();
bool midiEventPop(MidiEvent& event);

void midiInit(uart_inst_t* uart, uint rxPin);
void midiPoll();

} // namespace midi

#endif // MIDI_INPUT_H
