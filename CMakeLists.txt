# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

# Pull in Pico Extras (must be before project)
include(pico_extras_import.cmake)

project(supersaw_midi_synth C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Find all source files in subdirectories
file(GLOB_RECURSE SYNTH_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/synth/*.cpp")
file(GLOB_RECURSE MIDI_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/midi/*.cpp")
file(GLOB_RECURSE AUDIO_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/audio/*.cpp")
file(GLOB_RECURSE CONFIG_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/config/*.cpp")

add_executable(supersaw_midi_synth
    src/main.cpp
    ${SYNTH_SOURCES}
    ${MIDI_SOURCES}
    ${AUDIO_SOURCES}
    ${CONFIG_SOURCES}
)

# binary info (readable by picotool)
pico_set_program_name(supersaw_midi_synth "Supersaw MIDI Synth")
pico_set_program_version(supersaw_midi_synth "0.1")
pico_set_program_description(supersaw_midi_synth "MIDI controlled supersaw synthesizer")
pico_set_program_url(supersaw_midi_synth "https://github.com/woeps/supersaw-midi-synth")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(supersaw_midi_synth 0)
pico_enable_stdio_usb(supersaw_midi_synth 1)

# Add the standard library to the build
target_link_libraries(supersaw_midi_synth
    pico_stdlib
    pico_multicore
    pico_audio_i2s
    hardware_uart
    hardware_flash
)

# Add the standard include files to the build
target_include_directories(supersaw_midi_synth PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
)

pico_add_extra_outputs(supersaw_midi_synth)
