#!/usr/bin/env python3
"""
Generate precomputed lookup tables for the supersaw synth.

This script calculates:
1. midiNotePhaseInc[128] - Phase increments for MIDI notes 0-127
2. detuneMultiplier[7] - Detune multipliers for 7 oscillators

Usage:
    python3 scripts/generate_tables.py
"""

import math

SAMPLE_RATE = 44100
NUM_OSCILLATORS = 7


def compute_phase_inc(note: int) -> int:
    """
    Compute phase increment for a MIDI note.
    phase_inc = freq / SAMPLE_RATE * 2^32
    freq = 440 * 2^((note - 69) / 12)
    """
    freq = 440.0 * pow(2.0, (note - 69) / 12.0)
    return int(freq / SAMPLE_RATE * 4294967296.0)


def compute_detune_multiplier(index: int) -> int:
    """
    Compute detune multiplier in Q16.16 fixed-point.
    Oscillator order: [-3/3, -2/3, -1/3, 0, +1/3, +2/3, +3/3] * 0.3 semitones
    multiplier = 2^(offset * 0.3 / 12), stored as (multiplier * 65536)
    """
    offset = (index - 3) / 3.0  # range -1.0 to +1.0
    semitones = offset * 0.3
    mult = pow(2.0, semitones / 12.0)
    return int(mult * 65536.0 + 0.5)


def main():
    print("// Precomputed phase increments for MIDI notes 0–127.")
    print("// phase_inc = freq / SAMPLE_RATE * 2^32")
    print("// freq = 440 * 2^((note - 69) / 12)")
    print("const uint32_t midiNotePhaseInc[128] = {")

    phase_incs = [compute_phase_inc(note) for note in range(128)]
    for i in range(0, 128, 4):
        vals = phase_incs[i : i + 4]
        print("    " + ", ".join(str(v) for v in vals) + ",")

    print("};")
    print()

    print("// Detune multipliers in Q16.16 fixed-point.")
    print("// Oscillator order: [-3/3, -2/3, -1/3, 0, +1/3, +2/3, +3/3] * 0.3 semitones")
    print("// multiplier = 2^(offset * 0.3 / 12), stored as (multiplier * 65536)")
    print(f"const uint32_t detuneMultiplier[{NUM_OSCILLATORS}] = {{")

    detune_mults = [compute_detune_multiplier(i) for i in range(NUM_OSCILLATORS)]
    print("    " + ", ".join(str(v) for v in detune_mults) + ",")

    print("};")


if __name__ == "__main__":
    main()
