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
- Add support for multiple voices.
- Add envelope.
- Add support for more midi messages to control all parameters.
- Add support for more parameters.

### 3. Final supersaw-midi-synth

- Add hardware controls for every parameter.
- Try to add a filter for the output signal. - if performance and rp2040 capabilities allow
    - lowpass, highpass, bandpass
    - supporting cutoff frequency and resonance parameters
- Add support for more midi messages.
