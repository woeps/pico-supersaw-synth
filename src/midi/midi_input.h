#ifndef MIDI_INPUT_H
#define MIDI_INPUT_H

#include <cstdint>
#include "hardware/uart.h"

namespace midi {

// MIDI event packed into 32 bits for multicore FIFO
// Bit layout: [31:16 unused] [15:8 note] [7:1 unused] [0 gate]
struct MidiEvent {
    uint8_t note;
    bool gate;

    uint32_t pack() const {
        return (static_cast<uint32_t>(note) << 8) | (gate ? 1u : 0u);
    }

    static MidiEvent unpack(uint32_t data) {
        MidiEvent e;
        e.note = static_cast<uint8_t>((data >> 8) & 0xFF);
        e.gate = (data & 1u) != 0;
        return e;
    }
};

bool midiEventAvailable();
bool midiEventPop(MidiEvent& event);

void midiInit(uart_inst_t* uart, uint rxPin);
void midiPoll();

} // namespace midi

#endif // MIDI_INPUT_H
