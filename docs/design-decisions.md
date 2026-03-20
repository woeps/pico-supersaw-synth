# Design Decisions

## Fixed-Point Arithmetic in the Render Loop

The RP2040 has no hardware floating-point unit. All float operations are emulated in software, making them ~10-20x slower than integer operations. The audio render loop runs at 44100 Hz and must process 7 oscillators per sample, so performance is critical.

All render-loop math uses:
- **32-bit phase accumulators** for oscillator state
- **Q16.16 fixed-point** for detune multipliers
- **Integer multiply + shift** for division by 7

Float is only used during `noteOn()` (infrequent) for computing phase increments from the lookup table.

## pico_audio_i2s (pico-extras)

The SDK's `pico_audio_i2s` library from pico-extras handles the low-level I2S protocol via PIO and DMA. This provides:
- Double/triple buffered audio output
- Automatic DMA transfers
- Correct I2S timing

The alternative (raw PIO I2S) would give more control but significantly more code for no benefit at this stage.

## Dual-Core Architecture

- **Core 0**: Audio rendering (time-critical, blocks on DMA buffer availability)
- **Core 1**: MIDI polling (latency-sensitive, must not be blocked by audio)

This prevents MIDI input from being starved when audio rendering is busy waiting for buffers. The inter-core communication is a simple volatile flag pattern (single-producer, single-consumer) which avoids the overhead of mutexes or queues.

## 7 Oscillators

The classic Roland JP-8000 supersaw uses 7 oscillators. While fewer (3 or 5) would be lighter on the CPU, 7 gives the characteristic thick sound. At 44100 Hz with integer math, the RP2040 at 125 MHz has sufficient headroom for 7 oscillators in a single voice.

## No Envelope (PoC)

The proof of concept uses simple gate on/off with a 5ms fade ramp. This keeps the code minimal while avoiding clicks. Full ADSR envelope support is planned for milestone 2.

## Last-Note Priority

With a single voice, when a new note arrives it immediately replaces the current note. This "last-note priority" is the simplest monophonic behavior and works well for lead/bass sounds.

## Sample Rate: 44100 Hz

Standard CD-quality sample rate. The PCM5102A supports both 44100 and 48000 Hz. 44100 Hz was chosen as the conventional default and requires slightly less CPU per second.
