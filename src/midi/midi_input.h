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

    uint32_t pack() const {
        return (static_cast<uint32_t>(channel & 0x0F) << 20) |
               (static_cast<uint32_t>(type) << 16) |
               (static_cast<uint32_t>(param1 & 0x7F) << 8) |
               static_cast<uint32_t>(param2 & 0x7F);
    }

    static MidiEvent unpack(uint32_t data) {
        MidiEvent e;
        e.channel = static_cast<uint8_t>((data >> 20) & 0x0F);
        e.type    = static_cast<MidiEventType>((data >> 16) & 0x03);
        e.param1  = static_cast<uint8_t>((data >> 8) & 0x7F);
        e.param2  = static_cast<uint8_t>(data & 0x7F);
        return e;
    }
};

bool midiEventAvailable();
bool midiEventPop(MidiEvent& event);

void midiInit(uart_inst_t* uart, uint rxPin);
void midiPoll();

} // namespace midi

#endif // MIDI_INPUT_H
