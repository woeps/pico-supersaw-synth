#include "midi/midi_input.h"
#include "hardware/gpio.h"
#include "pico/multicore.h"
#include "config/pins.h"

namespace midi {

static uart_inst_t* midiUart = nullptr;

enum class ParserState : uint8_t {
    IDLE,
    DATA1_RECEIVED
};

static ParserState parserState = ParserState::IDLE;
static uint8_t statusByte = 0;
static uint8_t data1Byte = 0;

void midiInit(uart_inst_t* uart, uint rxPin) {
    midiUart = uart;

    uart_init(midiUart, MIDI_BAUD_RATE);
    gpio_set_function(rxPin, GPIO_FUNC_UART);

    // 8N1 is the default for uart_init, no extra config needed
}

bool midiEventAvailable() {
    return multicore_fifo_rvalid();
}

bool midiEventPop(MidiEvent& event) {
    if (!multicore_fifo_rvalid()) {
        return false;
    }
    uint32_t data = multicore_fifo_pop_blocking();
    event = MidiEvent::unpack(data);
    return true;
}

static void dispatchEvent(uint8_t status, uint8_t d1, uint8_t d2) {
    uint8_t msgType = status & 0xF0;

    if (msgType == 0x90 && d2 > 0) {
        // Note On
        MidiEvent event{MidiEventType::NOTE_ON, d1, d2};
        multicore_fifo_push_blocking(event.pack());
    } else if (msgType == 0x80 || (msgType == 0x90 && d2 == 0)) {
        // Note Off (explicit or velocity-0 convention)
        MidiEvent event{MidiEventType::NOTE_OFF, d1, d2};
        multicore_fifo_push_blocking(event.pack());
    } else if (msgType == 0xB0) {
        // Control Change
        MidiEvent event{MidiEventType::CC, d1, d2};
        multicore_fifo_push_blocking(event.pack());
    }
}

void midiPoll() {
    while (uart_is_readable(midiUart)) {
        uint8_t byte = uart_getc(midiUart);

        if (byte >= 0xF8) {
            // System real-time — ignore, don't touch parser state
            continue;
        }

        if (byte & 0x80) {
            // Status byte
            uint8_t msgType = byte & 0xF0;
            if (msgType == 0x80 || msgType == 0x90 || msgType == 0xB0) {
                statusByte = byte;
                parserState = ParserState::IDLE;
            } else {
                // Unsupported message type — reset
                statusByte = 0;
                parserState = ParserState::IDLE;
            }
        } else {
            // Data byte
            if (statusByte == 0) {
                // No valid status — ignore
                continue;
            }

            switch (parserState) {
                case ParserState::IDLE:
                    // First data byte: note number or CC number
                    data1Byte = byte;
                    parserState = ParserState::DATA1_RECEIVED;
                    break;

                case ParserState::DATA1_RECEIVED:
                    // Second data byte: velocity or CC value
                    dispatchEvent(statusByte, data1Byte, byte);
                    // Support running status: stay ready for next data pair
                    parserState = ParserState::IDLE;
                    break;
            }
        }
    }
}

} // namespace midi
