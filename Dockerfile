FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    libusb-1.0-0-dev \
    pkg-config \
    git \
    python3 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ARG PICO_SDK_VERSION=2.1.1
RUN git clone --recursive --branch ${PICO_SDK_VERSION} \
    https://github.com/raspberrypi/pico-sdk.git /pico-sdk

ARG PICO_EXTRAS_VERSION=sdk-2.1.1
RUN git clone --recursive --branch ${PICO_EXTRAS_VERSION} \
    https://github.com/raspberrypi/pico-extras.git /pico-extras

ARG PICOTOOL_VERSION=2.1.1
RUN git clone --recursive --branch ${PICOTOOL_VERSION} \
    https://github.com/raspberrypi/picotool.git /tmp/picotool \
    && cmake -S /tmp/picotool -B /tmp/picotool/build -G Ninja \
    && ninja -C /tmp/picotool/build \
    && cp /tmp/picotool/build/picotool /usr/local/bin/ \
    && rm -rf /tmp/picotool

ENV PICO_SDK_PATH=/pico-sdk
ENV PICO_EXTRAS_PATH=/pico-extras

WORKDIR /workspace
