# SUPERSAW-MIDI-SYNTH

Simple midi controlled supersaw synthesizer.

## Hardware

- raspberry pi pico (rp2040) microcontroller
- PCM5102A DAC
- serial MIDI-Input (UART)
- Audio Output (TRS)

## Technology

- written in C++ with pico-sdk
- focus on resource efficiency and simplicity

## Synthesizer

- The only supported waveform is a supersaw.
- Ideally, supporting up to 4 voices would be the goal.

### Parameters

- Envelope
    - attack
    - decay
    - sustain
    - release
- Detune
- Spread

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

TODO: research the original algorithm more deeply and try to adapt the current implementation to get closer to the original sound.

- randomize phase of each saw wave

### 6. effects

Experiment and try to see what is possible to implement on the rp2040:

- chorus
- delay
- reverb