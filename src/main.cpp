#include <stdio.h>
#include <cstring>
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
static uint32_t     lastBootselPollMs = 0;
static bool         cachedBootselState = false;
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
static struct audio_buffer_pool* audioPool = nullptr;

// Pre-fill audio buffers with silence to cover flash erase/program duration (~50 ms).
// This prevents I2S DMA underrun during the preset save operation.
static void prefillSilenceForFlash() {
    for (int i = 0; i < AUDIO_BUFFER_COUNT; i++) {
        struct audio_buffer* buf = take_audio_buffer(audioPool, false);
        if (buf) {
            memset(buf->buffer->bytes, 0, buf->max_sample_count * 4);
            buf->sample_count = buf->max_sample_count;
            give_audio_buffer(audioPool, buf);
        }
    }
}

void core1_entry() {
    multicore_lockout_victim_init();
    midi::midiInit(MIDI_UART, MIDI_RX_PIN);

    while (true) {
        midi::midiPoll();

        // Check if Core 0 has requested a voice render
        uint32_t cmd = supersaw.core1RenderCmd;
        if (cmd != 0) {
            supersaw.renderCore1Voices(cmd);
            __dmb(); // Ensure render data is committed before signaling done
            supersaw.core1RenderCmd = 0; // Signal completion (0 = idle)
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
    
    // Initialize onboard LED (active-high on the Raspberry Pi Pico)
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);

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

    audioPool = audio::getAudioBufferPool();

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

        // Memory barrier: ensures all voice/parameter writes are visible to Core 1.
        // Core 1 is guaranteed idle at this point because render() waits for
        // core1RenderDone before returning, so there's no concurrent access.
        __dmb();

        // ── BOOTSEL button state machine ──────────────────────────────
        uint32_t nowMs = to_ms_since_boot(get_absolute_time());
        if (nowMs - lastBootselPollMs >= 100) {
            cachedBootselState = get_bootsel_button();
            lastBootselPollMs = nowMs;
        }
        bool btnPressed = cachedBootselState;

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
                    // Pre-fill audio buffers with silence to cover flash erase (~50 ms)
                    prefillSilenceForFlash();
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
        // The Pico has a single (mono) onboard LED, so the RGB colour cues
        // used on the Tiny2040 are reduced to on/off + blink patterns:
        //   - save pending (held 3-5 s) → continuous blink
        //   - restore confirmed         → single solid flash
        //   - save confirmed            → triple flash
        //   - idle                      → on while any voice is active
        bool ledOn;
        switch (btnState) {
            case ButtonState::BLINK_RED:
                ledOn = ((nowMs / BLINK_PERIOD_MS) & 1) == 0;
                break;
            case ButtonState::FLASH_BLUE:
                // Restore: single flash → solid on for the whole window.
                ledOn = true;
                break;
            case ButtonState::FLASH_GREEN: {
                // Save: triple flash → 3 on-pulses across the flash window.
                // 6 equal slots (on/off/on/off/on/off) yield three blinks.
                uint32_t elapsed = nowMs - (btnFlashEndMs - FLASH_DURATION_MS);
                uint32_t slot = elapsed / (FLASH_DURATION_MS / 6);
                ledOn = (slot & 1) == 0;
                break;
            }
            default:
                ledOn = supersaw.anyVoiceActive();
                break;
        }
        gpio_put(LED_PIN, ledOn ? 1 : 0);

        // Get an audio buffer from the pool (blocks until available)
        struct audio_buffer* buffer = take_audio_buffer(audioPool, true);
        if (buffer) {
            int16_t* samples = (int16_t*)buffer->buffer->bytes;
            uint32_t numStereoSamples = buffer->max_sample_count;

            supersaw.render(samples, numStereoSamples);

            buffer->sample_count = numStereoSamples;
            give_audio_buffer(audioPool, buffer);
        }

        // Debug: light the LED to indicate digital clipping at the merge stage.
        // The LED stays on for 100 ms each time clipping is detected.
        {
            static uint32_t clipLedOffMs = 0;
            if (supersaw.dbgClipCount > 0) {
                supersaw.dbgClipCount = 0;
                clipLedOffMs = nowMs + 100;
            }
            if (clipLedOffMs > nowMs) {
                gpio_put(LED_PIN, 1);   // active-high: 1 = ON
            }
        }
    }

    return 0;
}
