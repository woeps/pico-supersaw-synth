#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/audio_i2s.h"
#include "hardware/gpio.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#include "hardware/resets.h"
#include "hardware/structs/timer.h"
#include "config/pins.h"
#include "config/preset_store.h"
#include "synth/supersaw.h"
#include "midi/midi_input.h"
#include "audio/audio_output.h"

// ─── BOOTSEL button reader ──────────────────────────────────────────────────
// Must reside in RAM (not flash) because it temporarily disables flash XiP.
static bool __no_inline_not_in_flash_func(get_bootsel_button)() {
    const uint CS_PIN_INDEX = 1;
    multicore_lockout_start_blocking();
    uint32_t flags = save_and_disable_interrupts();
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    for (volatile int i = 0; i < 1000; ++i);
    bool button_state = !(sio_hw->gpio_hi_in & (1u << CS_PIN_INDEX));
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);
    restore_interrupts(flags);
    multicore_lockout_end_blocking();
    return button_state;
}

// ─── Button state machine ───────────────────────────────────────────────────
enum class ButtonState : uint8_t {
    IDLE,         // button not pressed
    HELD,         // pressed, < 3 s
    BLINK_RED,    // held >= 3 s, awaiting save
    FLASH_GREEN,  // save confirmed
    FLASH_BLUE,   // restore confirmed
};

static ButtonState  btnState       = ButtonState::IDLE;
static uint32_t     btnPressTimeMs = 0;
static uint32_t     btnFlashEndMs  = 0;
static constexpr uint32_t HOLD_THRESHOLD_MS = 3000;
static constexpr uint32_t SAVE_THRESHOLD_MS = 5000;
static constexpr uint32_t FLASH_DURATION_MS = 500;
static constexpr uint32_t BLINK_PERIOD_MS   = 250;

// Helper: restore preset CCs into the synth engine.
static void applyPreset(synth::Supersaw& ss, const preset_store::Preset& p) {
    for (int i = 0; i < PRESET_CC_COUNT; i++) {
        ss.setCC(preset_store::presetCCMap[i], p.cc[i]);
    }
}

// Helper: build a Preset from current synth state.
static void capturePreset(const synth::Supersaw& ss, preset_store::Preset& p) {
    p.magic   = preset_store::PRESET_MAGIC;
    p.version = preset_store::PRESET_VERSION;
    for (int i = 0; i < PRESET_CC_COUNT; i++) {
        p.cc[i] = ss.getCC(preset_store::presetCCMap[i]);
    }
}

static synth::Supersaw supersaw;

void core1_entry() {
    multicore_lockout_victim_init();
    midi::midiInit(MIDI_UART, MIDI_RX_PIN);

    while (true) {
        midi::midiPoll();

        // Check if Core 0 has requested a voice render
        uint32_t cmd = supersaw.core1RenderCmd;
        if (cmd != 0 && !supersaw.core1RenderDone) {
            supersaw.renderCore1Voices(cmd);
            __dmb(); // Ensure render data is committed before flag is set
            supersaw.core1RenderDone = true;
        }
    }
}


// Recover from SWD debug flash/soft-reset by normalizing peripherals
static void recover_from_swd_reset() {
    // Un-pause the hardware timer (frozen by SWD halt) so sleep_ms() works
    timer_hw->dbgpause = 0;

    // Hard-reset peripherals that might be left running by a previous session
    uint32_t reset_mask = RESETS_RESET_PIO0_BITS | RESETS_RESET_PIO1_BITS | 
                          RESETS_RESET_DMA_BITS | RESETS_RESET_UART1_BITS;
    reset_block(reset_mask);
    unreset_block_wait(reset_mask);
}

int main() {
    recover_from_swd_reset();

    stdio_init_all();
    printf("Supersaw MIDI Synth starting...\n");
    
    // Initialize onboard RGB LED
    gpio_init(LED_RED_PIN);
    gpio_init(LED_GREEN_PIN);
    gpio_init(LED_BLUE_PIN);
    gpio_set_dir(LED_RED_PIN, GPIO_OUT);
    gpio_set_dir(LED_GREEN_PIN, GPIO_OUT);
    gpio_set_dir(LED_BLUE_PIN, GPIO_OUT);
    gpio_put(LED_RED_PIN, 1);
    gpio_put(LED_GREEN_PIN, 1);
    gpio_put(LED_BLUE_PIN, 1);

    supersaw.init();

    // Restore saved preset from flash (if valid)
    preset_store::Preset loadedPreset;
    if (preset_store::load(loadedPreset)) {
        applyPreset(supersaw, loadedPreset);
        printf("Preset restored from flash.\n");
    }

    multicore_reset_core1();
    sleep_ms(100); // Required for Core 1 to properly start after SWD flash
    multicore_launch_core1(core1_entry);
    printf("Core 1 launched (MIDI input + voice rendering).\n");

    audio::audioInit();

    struct audio_buffer_pool* pool = audio::getAudioBufferPool();

    while (true) {
        // Process all pending MIDI events from core 1
        midi::MidiEvent event;
        while (midi::midiEventPop(event)) {
            switch (event.type) {
                case midi::MidiEventType::NOTE_ON:
                    supersaw.noteOn(event.param1, event.param2);
                    break;
                case midi::MidiEventType::NOTE_OFF:
                    supersaw.noteOff(event.param1);
                    break;
                case midi::MidiEventType::CC:
                    supersaw.setCC(event.param1, event.param2);
                    break;
                case midi::MidiEventType::PANIC:
                    supersaw.panic();
                    break;
            }
        }

        // ── BOOTSEL button state machine ──────────────────────────────
        uint32_t nowMs = to_ms_since_boot(get_absolute_time());
        bool btnPressed = get_bootsel_button();

        switch (btnState) {
            case ButtonState::IDLE:
                if (btnPressed) {
                    btnPressTimeMs = nowMs;
                    btnState = ButtonState::HELD;
                }
                break;

            case ButtonState::HELD:
                if (!btnPressed) {
                    // Released before 3 s → restore preset
                    preset_store::Preset p;
                    if (preset_store::load(p)) {
                        applyPreset(supersaw, p);
                        printf("Preset restored (short press).\n");
                    }
                    btnFlashEndMs = nowMs + FLASH_DURATION_MS;
                    btnState = ButtonState::FLASH_BLUE;
                } else if (nowMs - btnPressTimeMs >= HOLD_THRESHOLD_MS) {
                    btnState = ButtonState::BLINK_RED;
                }
                break;

            case ButtonState::BLINK_RED:
                if (!btnPressed) {
                    // Released between 3–5 s without reaching save
                    btnState = ButtonState::IDLE;
                } else if (nowMs - btnPressTimeMs >= SAVE_THRESHOLD_MS) {
                    // Held 5 s → save preset
                    preset_store::Preset p;
                    capturePreset(supersaw, p);
                    preset_store::save(p);
                    printf("Preset saved to flash.\n");
                    btnFlashEndMs = nowMs + FLASH_DURATION_MS;
                    btnState = ButtonState::FLASH_GREEN;
                }
                break;

            case ButtonState::FLASH_GREEN:
            case ButtonState::FLASH_BLUE:
                if (nowMs >= btnFlashEndMs) {
                    btnState = ButtonState::IDLE;
                }
                break;
        }

        // ── LED control ──────────────────────────────────────────────
        switch (btnState) {
            case ButtonState::BLINK_RED: {
                bool on = ((nowMs / BLINK_PERIOD_MS) & 1) == 0;
                gpio_put(LED_RED_PIN, on ? 0 : 1);
                gpio_put(LED_GREEN_PIN, 1);
                gpio_put(LED_BLUE_PIN, 1);
                break;
            }
            case ButtonState::FLASH_GREEN:
                gpio_put(LED_RED_PIN, 1);
                gpio_put(LED_GREEN_PIN, 0);
                gpio_put(LED_BLUE_PIN, 1);
                break;
            case ButtonState::FLASH_BLUE:
                gpio_put(LED_RED_PIN, 1);
                gpio_put(LED_GREEN_PIN, 1);
                gpio_put(LED_BLUE_PIN, 0);
                break;
            default:
                // Normal LED: green while any voice is active
                if (supersaw.anyVoiceActive()) {
                    gpio_put(LED_RED_PIN, 1);
                    gpio_put(LED_GREEN_PIN, 0);
                    gpio_put(LED_BLUE_PIN, 1);
                } else {
                    gpio_put(LED_RED_PIN, 1);
                    gpio_put(LED_GREEN_PIN, 1);
                    gpio_put(LED_BLUE_PIN, 1);
                }
                break;
        }

        // Get an audio buffer from the pool (blocks until available)
        struct audio_buffer* buffer = take_audio_buffer(pool, true);
        if (buffer) {
            int16_t* samples = (int16_t*)buffer->buffer->bytes;
            uint32_t numStereoSamples = buffer->max_sample_count;

            supersaw.render(samples, numStereoSamples);

            buffer->sample_count = numStereoSamples;
            give_audio_buffer(pool, buffer);
        }

        // Debug: use red LED to indicate digital clipping at the merge stage.
        // The LED stays red for 100 ms each time clipping is detected.
        {
            static uint32_t clipLedOffMs = 0;
            if (supersaw.dbgClipCount > 0) {
                supersaw.dbgClipCount = 0;
                clipLedOffMs = nowMs + 100;
            }
            if (clipLedOffMs > nowMs) {
                gpio_put(LED_RED_PIN, 0);   // active-low: 0 = ON
                gpio_put(LED_GREEN_PIN, 1); // turn off green
            }
        }
    }

    return 0;
}
