# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later

#
# CMake configuration file for the Radtel RT-950 Pro OpenRTX target
#
cmake_minimum_required(VERSION 3.20.0)

target_sources(app
  PRIVATE
    ${OPENRTX_ROOT}/platform/mcu/AT32F403A/drivers/delays.c
    ${OPENRTX_ROOT}/platform/drivers/GPIO/gpio_zephyr.c
    ${OPENRTX_ROOT}/platform/drivers/SPI/spi_zephyr.c
    ${OPENRTX_ROOT}/platform/drivers/I2C/i2c_bitbang.c

    ${OPENRTX_ROOT}/platform/mcu/AT32F403A/drivers/rtc.c
    ${OPENRTX_ROOT}/platform/drivers/GPS/nmea_rbuf.c
    ${OPENRTX_ROOT}/platform/drivers/GPS/gps_zephyr.c

    ${OPENRTX_ROOT}/platform/drivers/NVM/nvmem_rt950pro.c
    ${OPENRTX_ROOT}/platform/drivers/baseband/BK4829.c
    ${OPENRTX_ROOT}/platform/drivers/baseband/SI4732.c

    ${OPENRTX_ROOT}/platform/drivers/display/display_rt950pro.c
    ${OPENRTX_ROOT}/platform/drivers/keyboard/keyboard_rt950pro.c

    ${OPENRTX_ROOT}/platform/drivers/audio/audio_rt950pro.c
    ${OPENRTX_ROOT}/platform/drivers/audio/at32_adc.c
    ${OPENRTX_ROOT}/platform/drivers/audio/at32_dac.c
    ${OPENRTX_ROOT}/platform/drivers/baseband/radio_rt950pro.cpp
    ${OPENRTX_ROOT}/platform/drivers/baseband/BK4829_tone.cpp
    ${OPENRTX_ROOT}/platform/drivers/stubs/cps_io_stub.c

    ${OPENRTX_ROOT}/platform/targets/rt950pro/bluetooth_rt950pro.c
    ${OPENRTX_ROOT}/platform/targets/rt950pro/bootloader.c
    ${OPENRTX_ROOT}/platform/targets/rt950pro/monitor.c
    ${OPENRTX_ROOT}/platform/targets/rt950pro/board_init.c
    ${OPENRTX_ROOT}/platform/targets/rt950pro/hwconfig.c
    ${OPENRTX_ROOT}/platform/targets/rt950pro/platform.c
)

# Vendor bootloader model signatures, right after the vector table
# Codec2-mod and the M17 modem for speed, not for size: M17 has little CPU
# to spare here. Single precision constants keep the codec's double
# literals off the software floating point routines, the FPU being single
# precision.
file(GLOB RT950PRO_CODEC2_SOURCES
     ${OPENRTX_ROOT}/subprojects/codec2-mod/src/*.c)
file(GLOB RT950PRO_M17_SOURCES ${OPENRTX_ROOT}/openrtx/src/protocols/M17/*.cpp)
set_source_files_properties(${RT950PRO_CODEC2_SOURCES}
                            TARGET_DIRECTORY app
                            PROPERTIES COMPILE_OPTIONS
                                       "-O2;-fsingle-precision-constant")
set_source_files_properties(${RT950PRO_M17_SOURCES}
                            TARGET_DIRECTORY app
                            PROPERTIES COMPILE_OPTIONS "-O2")

zephyr_linker_sources(ROM_START SORT_KEY 0x3rt950 rt950pro_model_sig.ld)

# Package the raw binary in the .BTF format accepted by the vendor bootloader
set(RT950PRO_BTF ${CMAKE_BINARY_DIR}/openrtx_rt950pro.BTF)

set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
  COMMAND ${PYTHON_EXECUTABLE} ${OPENRTX_ROOT}/scripts/rt950_btf.py
          ${PROJECT_BINARY_DIR}/${CONFIG_KERNEL_BIN_NAME}.bin
          ${RT950PRO_BTF}
)

set_property(GLOBAL APPEND PROPERTY extra_post_build_byproducts
  ${RT950PRO_BTF}
)

target_include_directories(app
  PRIVATE
    ${OPENRTX_ROOT}/platform/targets/rt950pro
)
