#!/usr/bin/env python3
"""
Generate the piecewise-exponential filter cutoff coefficient table.

The curve is split into two segments for finer control at high frequencies:
  - CC   0–80:  20 Hz  →  8 kHz  (exponential, ~8.6 octaves in 81 steps)
  - CC  80–127:  8 kHz → 16 kHz  (exponential,  1   octave  in 47 steps)

This gives roughly 5× more resolution in the top octave compared to a
single exponential spanning the full range.

Each table entry stores: sin(π × fc / SAMPLE_RATE) × 2^14  (Q14)
which is the cutoff coefficient fed directly into the Chamberlin SVF.

Usage:
    python3 scripts/generate_filter_table.py          # print table to stdout
    python3 scripts/generate_filter_table.py --write   # patch filter.cpp in-place
"""

import argparse
import math
import os
import re

SAMPLE_RATE = 44100

# Piecewise breakpoints
CC_BREAK = 80          # CC index where segments join
FREQ_MIN = 20.0        # Hz at CC 0
FREQ_BREAK = 8000.0    # Hz at CC_BREAK
FREQ_MAX = 16000.0     # Hz at CC 127


def compute_cutoff_table() -> list[int]:
    """Return 128 Q14 cutoff coefficients for the piecewise-exponential curve."""
    table = []
    for cc in range(128):
        if cc <= CC_BREAK:
            # Segment 1: exponential from FREQ_MIN to FREQ_BREAK
            t = cc / CC_BREAK
            fc = FREQ_MIN * pow(FREQ_BREAK / FREQ_MIN, t)
        else:
            # Segment 2: exponential from FREQ_BREAK to FREQ_MAX
            t = (cc - CC_BREAK) / (127 - CC_BREAK)
            fc = FREQ_BREAK * pow(FREQ_MAX / FREQ_BREAK, t)
        # Limit fc to avoid infinity approaching Nyquist
        if fc > 22000.0:
            fc = 22000.0
            
        coeff = math.tan(math.pi * fc / SAMPLE_RATE) * 16384.0
        table.append(max(1, int(coeff + 0.5)))
    return table


def format_table(table: list[int]) -> str:
    """Format the table as a C int32_t array initialiser."""
    lines = []
    for i in range(0, 128, 8):
        chunk = table[i : i + 8]
        lines.append("    " + ", ".join(f"{v:5d}" for v in chunk) + ",")
    return "\n".join(lines)


def print_table(table: list[int]):
    """Print the C table and some diagnostic info to stdout."""
    print(f"// Piecewise-exponential cutoff coefficients (Q14):")
    print(f"//   CC   0–{CC_BREAK}:  {FREQ_MIN:.0f} Hz → {FREQ_BREAK:.0f} Hz")
    print(f"//   CC {CC_BREAK}–127: {FREQ_BREAK:.0f} Hz → {FREQ_MAX:.0f} Hz")
    print(f"// coeff = tan(π × fc / {SAMPLE_RATE}) × 16384")
    print("const int32_t filterCutoffTable[128] = {")
    print(format_table(table))
    print("};")
    print()

    # Diagnostics: show frequency at each CC
    print("// CC → frequency mapping:")
    for cc in range(128):
        if cc <= CC_BREAK:
            t = cc / CC_BREAK
            fc = FREQ_MIN * pow(FREQ_BREAK / FREQ_MIN, t)
        else:
            t = (cc - CC_BREAK) / (127 - CC_BREAK)
            fc = FREQ_BREAK * pow(FREQ_MAX / FREQ_BREAK, t)
        print(f"//   CC {cc:3d} → {fc:8.1f} Hz  (coeff = {table[cc]})")


def patch_filter_cpp(table: list[int]):
    """Replace the cutoff table in filter.cpp in-place."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    filter_cpp = os.path.join(project_root, "src", "synth", "filter.cpp")

    with open(filter_cpp, "r") as f:
        content = f.read()

    # Build replacement block
    comment = (
        f"// Piecewise-exponential cutoff coefficients (Q14):\n"
        f"//   CC   0–{CC_BREAK}:  {FREQ_MIN:.0f} Hz → {FREQ_BREAK:.0f} Hz  "
        f"(exponential)\n"
        f"//   CC {CC_BREAK}–127: {FREQ_BREAK:.0f} Hz → {FREQ_MAX:.0f} Hz  "
        f"(exponential, finer resolution)\n"
        f"// coeff = tan(π × fc / {SAMPLE_RATE}) × 16384"
    )
    array_body = format_table(table)
    replacement = (
        f"{comment}\n"
        f"const int32_t filterCutoffTable[128] = {{\n"
        f"{array_body}\n"
        f"}};"
    )

    # Match the existing comment + table declaration
    pattern = (
        r"//[^\n]*cutoff[^\n]*\n"        # first comment line
        r"(?://[^\n]*\n)*"               # any additional comment lines
        r"const int32_t filterCutoffTable\[128\]\s*=\s*\{[^}]+\};"
    )
    new_content, count = re.subn(pattern, replacement, content, flags=re.IGNORECASE)
    if count == 0:
        print("ERROR: Could not find filterCutoffTable in", filter_cpp)
        return

    with open(filter_cpp, "w") as f:
        f.write(new_content)
    print(f"Patched {filter_cpp} ({count} replacement(s))")


def main():
    parser = argparse.ArgumentParser(
        description="Generate piecewise-exponential filter cutoff LUT."
    )
    parser.add_argument(
        "--write", action="store_true",
        help="Patch the table directly into src/synth/filter.cpp"
    )
    args = parser.parse_args()

    table = compute_cutoff_table()

    if args.write:
        patch_filter_cpp(table)
    else:
        print_table(table)


if __name__ == "__main__":
    main()
