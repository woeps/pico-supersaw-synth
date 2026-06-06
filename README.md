# SUPERSAW-MIDI-SYNTH

[![Build Firmware](https://github.com/woeps/pico-supersaw-synth/actions/workflows/build-firmware.yml/badge.svg)](https://github.com/woeps/pico-supersaw-synth/actions/workflows/build-firmware.yml)

Simple midi controlled supersaw synthesizer.

## Hardware

- raspberry pi pico (rp2040) microcontroller
- PCM5102A DAC
- serial MIDI-Input (UART)
- Audio Output (TRS)

## Bill of Materials

| Part | Quantity | Notes |
|------|----------|-------|
| Breadboard / PCB | 1 | |
| Raspberry Pi Pico (RP2040) | 1 | |
| PCM5102A DAC module | 1 | |
| H11L1 optocoupler | 1 | MIDI isolation |
| 220Ω resistor | 1 | MIDI input current limiting |
| MIDI DIN 5-pin socket | 1 | or use a second 3.5mm TRS Jack |
| 3.5mm TRS jack | 1 | Audio output |
| Power supply | 1 | USB or 5V via VSYS |
| Wire | • | |

## Technology

- written in C++ with pico-sdk
- focus on resource efficiency and simplicity

## Synthesizer

- The only supported waveform is a supersaw.
- Ideally, supporting up to 4 voices would be the goal.

### Parameters

All parameters are controllable via MIDI CC:

| CC | Parameter | Range |
|----|-----------|-------|
| 70 | Filter mode | 0=LPF, 1=HPF, 2=BPF |
| 71 | Filter resonance | 0–127 |
| 72 | Envelope release | 0–127 |
| 73 | Envelope attack | 0–127 |
| 74 | Filter cutoff | 0–127 |
| 75 | Envelope decay | 0–127 |
| 79 | Envelope sustain | 0–127 |
| 85 | Pitch bend range | 0–24 semitones |
| 91 | Chorus depth | 0–127 |
| 92 | Chorus rate | 0–127 |
| 93 | Stereo spread | 0–127 |
| 94 | Detune | 0–127 |
| 95 | Mix (dry/wet) | 0–127 |

## Milestones

### 1. Proof of concept

- Implement the soundgenerator just for a single voice.
- Hardcode moderate values for spread and detune.
- No envelope.
- Only supported midi messages are note on and note off.
- No support for multiple voices.

### 2. Simple midi controlled supersaw synthesizer

- Build on top of the proof of concept.
- Add support for multiple voices. (up to 4)
- Add support for more parameters.
- Add envelope.
- Add support for more midi messages to control all parameters.

### 3. More parameters

- Add mix parameter
    - control the volume ratio of the several saw-waves between the one in the harmonic center (pitch according to midi note on) and the others
- Try to add a filter for the output signal. - if performance and rp2040 capabilities allow
    - highpass (implemented via Zero-Delay Feedback Trapezoidal SVF using RP2040 hardware divider)
    - lowpass, bandpass (prepared in the code, to be wired up)
    - supporting cutoff frequency and resonance parameters
    - support keytracking

### 4. hardware control

- Add support for more midi messages.
- add a hardware interface to control parameters

### 5. Fine-tune sound to get closer to jp8000 sound

research the original algorithm more deeply and try to adapt the current implementation to get closer to the original sound.

- randomize phase of each saw wave

### 6. effects

Experiment and try to see what is possible to implement on the rp2040:

- chorus
- delay
- reverb

## Ideas

- handle after-touch messages as filter cutoff modulation
- legato
