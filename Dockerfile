FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
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

ENV PICO_SDK_PATH=/pico-sdk
ENV PICO_EXTRAS_PATH=/pico-extras

WORKDIR /workspace
