# Building

## Prerequisites

- [Raspberry Pi Pico SDK](https://github.com/raspberrypi/pico-sdk) (v2.1.1)
- [Pico Extras](https://github.com/raspberrypi/pico-extras) (for `pico_audio_i2s`)
- CMake (>= 3.13)
- ARM GCC toolchain (`arm-none-eabi-gcc`)

## Environment Setup

Set the SDK and extras paths:

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
export PICO_EXTRAS_PATH=/path/to/pico-extras
```

Alternatively, if using the Pico VS Code extension, these are managed automatically via `~/.pico-sdk/`.

## Build Steps

```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

This produces `supersaw_midi_synth.uf2` in the `build/` directory.

## Flashing

1. Hold the BOOTSEL button on the Pico
2. Connect the Pico to your computer via USB (while holding BOOTSEL)
3. Release BOOTSEL — the Pico mounts as a USB mass storage device
4. Copy `build/supersaw_midi_synth.uf2` to the mounted drive
5. The Pico reboots and starts running the synth

Alternatively, use `picotool`:

```bash
picotool load build/supersaw_midi_synth.uf2
picotool reboot
```

or the picoprobe:

```bash
openocd -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "program ./supersaw_midi_synth.elf verify reset exit"
```

## CI Build Environment

CI builds run inside a prebuilt Docker image (`ghcr.io/<owner>/pico-build-env:2.1.1`) defined by the repository `Dockerfile` and published by the `Publish Build Environment` workflow. The image pins:

- Pico SDK `2.1.1`
- Pico Extras `sdk-2.1.1`
- picotool `2.1.1`

picotool is **installed** (not just copied) via `ninja -C build install` with `-DCMAKE_INSTALL_PREFIX=/usr/local`. This installs both the `picotool` binary (to `/usr/local/bin`) and its CMake package config (`picotoolConfig.cmake`, to `/usr/local/lib/cmake/picotool`). The SDK locates picotool with `find_package(picotool ... CONFIG)`; without the installed package config, the SDK falls back to downloading and rebuilding picotool from source on every firmware build. Installing the package config avoids that rebuild and silences the `Findpicotool.cmake` warning.

## Debug Output

USB stdio is enabled (`pico_enable_stdio_usb`). Connect the Pico via USB and open a serial terminal (e.g. `minicom`, `screen`, or PuTTY) on the USB CDC ACM port to see debug messages.
