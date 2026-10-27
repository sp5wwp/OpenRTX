# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build environment for the Retevis C62 (ListenAI CSK6011B, Zephyr 3.4 fork).
# No ListenAI "lisa" tool needed: west fetches the fork from OpenRTX's mirror
# and the stock Zephyr SDK 0.16.8 ARM toolchain builds it.
# See scripts/c62/README.md.

FROM ubuntu:24.04

ARG SDK_VERSION=0.16.8
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates git cmake ninja-build gperf device-tree-compiler \
        python3 python3-venv python3-pip wget xz-utils file make gcc g++ \
        ccache meson sudo curl unzip \
    && rm -rf /var/lib/apt/lists/*

# Zephyr SDK: only the arm-zephyr-eabi toolchain. x86_64 and arm64 hosts
# (Apple silicon) both get a native SDK.
ARG TARGETARCH
RUN case "${TARGETARCH:-amd64}" in \
        arm64) HOST=aarch64 ;; \
        *) HOST=x86_64 ;; \
    esac \
    && URL=https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v${SDK_VERSION} \
    && cd /opt \
    && wget -q -O- $URL/zephyr-sdk-${SDK_VERSION}_linux-${HOST}_minimal.tar.xz | tar xJ \
    && cd zephyr-sdk-${SDK_VERSION} \
    && wget -q -O- $URL/toolchain_linux-${HOST}_arm-zephyr-eabi.tar.xz | tar xJ \
    && ./setup.sh -c

# west and Zephyr's Python requirements (scripts/requirements-base.txt)
ENV VIRTUAL_ENV=/opt/venv
ENV PATH="$VIRTUAL_ENV/bin:$PATH"
RUN python3 -m venv $VIRTUAL_ENV \
    && pip install --no-cache-dir west pyelftools PyYAML pykwalify canopen \
        packaging progress psutil pylink-square pyserial requests anytree \
        intelhex

ENV ZEPHYR_SDK_INSTALL_DIR=/opt/zephyr-sdk-${SDK_VERSION}
ENV ZEPHYR_16_SDK_INSTALL_DIR=/opt/zephyr-sdk-${SDK_VERSION}
ENV ZEPHYR_TOOLCHAIN_VARIANT=zephyr

COPY build.sh /usr/local/bin/c62-build
RUN chmod +x /usr/local/bin/c62-build

WORKDIR /work
CMD ["c62-build"]
