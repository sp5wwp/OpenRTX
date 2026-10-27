# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later

#
# CMake configuration file for the Retevis C62 OpenRTX target
#
cmake_minimum_required(VERSION 3.20.0)

zephyr_compile_definitions(PSRAM_CHIP_SIZE=4) # 4MBytes PSRAM!

target_sources(app
  PRIVATE
    ${OPENRTX_ROOT}/platform/mcu/CSK6011B/drivers/delays.c

    ${OPENRTX_ROOT}/platform/drivers/GPIO/gpio_zephyr.c
    ${OPENRTX_ROOT}/platform/drivers/GPIO/gpio_bk4819.c
    ${OPENRTX_ROOT}/platform/drivers/SPI/spi_zephyr.c
    ${OPENRTX_ROOT}/platform/drivers/SPI/spi_zephyr_bus.c
    ${OPENRTX_ROOT}/platform/drivers/I2C/i2c_bitbang.c

    ${OPENRTX_ROOT}/platform/drivers/baseband/BK4819.c
    ${OPENRTX_ROOT}/platform/drivers/baseband/BK1080.c

    ${OPENRTX_ROOT}/platform/drivers/stubs/nvmem_stub.c
    ${OPENRTX_ROOT}/platform/drivers/stubs/cps_io_stub.c

    ${OPENRTX_ROOT}/platform/drivers/display/display_c62.c
    ${OPENRTX_ROOT}/platform/drivers/baseband/radio_C62.cpp
    ${OPENRTX_ROOT}/platform/drivers/keyboard/keyboard_c62.c
    ${OPENRTX_ROOT}/platform/drivers/audio/audio_c62.c
    ${OPENRTX_ROOT}/platform/drivers/audio/csk6_codec.c

    ${OPENRTX_ROOT}/platform/targets/c62/platform.c
    ${OPENRTX_ROOT}/platform/targets/c62/hwconfig.c
    ${OPENRTX_ROOT}/platform/targets/c62/dsp.c
    ${OPENRTX_ROOT}/platform/targets/c62/lsf.c
)

#
# Experimental RX options, set with e.g.
#   west build -b c62 -d build_c62 . -- -DC62_BASEBAND_DUMP_BAUD=2000000
# CMake caches them in the build directory: pass them again with a new value
# or use a fresh build directory to change them.
#
# C62_WIDE_RX: widest BK4819 RX channel filter in M17 mode (REG_43 0x7e28).
# C62_BASEBAND_DUMP: stream the raw 48 kHz RX audio over the console UART,
#   muting the log. See platform/drivers/audio/baseband_dump_c62.h and
#   scripts/c62_bbdump.py.
# C62_REG43_SWEEP: in M17 RX, cycle REG_43 through test values, each for
#   C62_REG43_SWEEP_MS. Use it with the dump to measure each setting.
#
option(C62_WIDE_RX "Widest BK4819 RX filter in M17 mode" ON)
option(C62_BASEBAND_DUMP "Stream raw RX baseband over UART" ON)
# The C62 programming cables top out at 115200 baud (11.5 kB/s), far below
# the 96 kB/s of a 48 kHz s16 stream: use burst mode there. Streaming needs
# at least 1500000 baud for s16 or 921600 for 12-bit samples.
set(C62_BASEBAND_DUMP_BAUD 115200 CACHE STRING "Baseband dump baud rate")
set(C62_BASEBAND_DUMP_FORMAT 1 CACHE STRING
    "Baseband dump sample format: 1 = s16, 2 = 12-bit packed")
option(C62_BASEBAND_DUMP_BURST
       "Fill 1 MiB of PSRAM, send it, repeat (gapless at any baud rate)" ON)
option(C62_REG43_SWEEP "Cycle REG_43 test values in M17 RX" OFF)
# C62_TETRA: TETRA TMO downlink receive mode ("T" under key 5), decoded with
#   libtetra, which west fetches to modules/lib/libtetra.
option(C62_TETRA "TETRA receive mode" ON)
# C62_TETRA_SPEECH: play TETRA speech with OpenACELP. Needs the ETSI
#   codebooks in modules/lib/openacelp/build/tetra_tables.c, which
#   scripts/c62/build.sh generates. AUTO (default): on when they are there.
#   A firmware built with them contains ETSI material: do not distribute it.
set(C62_REG43_SWEEP_MS 1400 CACHE STRING "REG_43 sweep dwell time, ms")

if(C62_WIDE_RX)
  target_compile_definitions(app PRIVATE C62_WIDE_RX)
endif()
if(C62_REG43_SWEEP)
  target_compile_definitions(app PRIVATE C62_REG43_SWEEP
                             C62_REG43_SWEEP_MS=${C62_REG43_SWEEP_MS})
endif()
if(C62_BASEBAND_DUMP)
  target_sources(app PRIVATE
    ${OPENRTX_ROOT}/platform/drivers/audio/baseband_dump_c62.c)
  target_compile_definitions(app PRIVATE C62_BASEBAND_DUMP
    C62_BASEBAND_DUMP_BAUD=${C62_BASEBAND_DUMP_BAUD}
    C62_BASEBAND_DUMP_FORMAT=${C62_BASEBAND_DUMP_FORMAT})
  if(C62_BASEBAND_DUMP_BURST)
    target_compile_definitions(app PRIVATE C62_BASEBAND_DUMP_BURST=1)
  else()
    target_compile_definitions(app PRIVATE C62_BASEBAND_DUMP_BURST=0)
  endif()
endif()
if(C62_TETRA)
  set(C62_LIBTETRA ${WEST_TOPDIR}/modules/lib/libtetra)
  if(NOT EXISTS ${C62_LIBTETRA}/CMakeLists.txt)
    message(FATAL_ERROR "libtetra not found in ${C62_LIBTETRA}: run "
                        "'west update --group-filter +csk6', or build with "
                        "-DC62_TETRA=OFF")
  endif()
  add_subdirectory(${C62_LIBTETRA} ${CMAKE_BINARY_DIR}/libtetra)
  # Build it with the Zephyr compiler flags (CPU, FPU, ABI)
  target_link_libraries(tetra PRIVATE zephyr_interface)
  target_link_libraries(app PRIVATE tetra)
  target_sources(app PRIVATE
    ${OPENRTX_ROOT}/openrtx/src/protocols/TETRA/TetraDemod.c
    ${OPENRTX_ROOT}/openrtx/src/protocols/TETRA/TetraStatus.c
    ${OPENRTX_ROOT}/openrtx/src/rtx/OpMode_TETRA.cpp)
  target_compile_definitions(app PRIVATE CONFIG_TETRA)

  set(C62_OPENACELP ${WEST_TOPDIR}/modules/lib/openacelp)
  set(C62_ETSI_TABLES ${C62_OPENACELP}/build/tetra_tables.c)
  # Re-run CMake when the codebooks appear in or leave that folder
  if(EXISTS ${C62_OPENACELP}/build)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 ${C62_OPENACELP}/build)
  endif()
  set(C62_TETRA_SPEECH AUTO CACHE STRING
      "TETRA speech with OpenACELP: AUTO (if the codebooks exist), ON, OFF")
  if(C62_TETRA_SPEECH STREQUAL "AUTO")
    if(EXISTS ${C62_ETSI_TABLES})
      set(c62_speech ON)
    else()
      set(c62_speech OFF)
    endif()
  else()
    set(c62_speech ${C62_TETRA_SPEECH})
  endif()
  if(c62_speech)
    if(NOT EXISTS ${C62_ETSI_TABLES})
      message(FATAL_ERROR "C62_TETRA_SPEECH needs ${C62_ETSI_TABLES}: see "
                          "scripts/c62/build.sh")
    endif()
    target_sources(app PRIVATE
      ${C62_OPENACELP}/src/oa_dsp.c
      ${C62_OPENACELP}/src/oa_bits.c
      ${C62_OPENACELP}/src/oa_dec.c
      ${C62_ETSI_TABLES})
    target_include_directories(app PRIVATE
      ${C62_OPENACELP}/include ${C62_OPENACELP}/src ${C62_OPENACELP}/build)
    target_compile_definitions(app PRIVATE CONFIG_TETRA_SPEECH)
    message(WARNING "C62: TETRA speech enabled. This firmware contains ETSI "
                    "codebooks: do not distribute it.")
  else()
    message(STATUS "C62: TETRA speech disabled (no ETSI codebooks)")
  endif()
  target_sources(app PRIVATE
    ${OPENRTX_ROOT}/openrtx/src/protocols/TETRA/TetraSpeech.c)
endif()

message(STATUS "C62: wide RX ${C62_WIDE_RX}, TETRA ${C62_TETRA}, REG_43 sweep ${C62_REG43_SWEEP}, "
               "baseband dump ${C62_BASEBAND_DUMP} "
               "(${C62_BASEBAND_DUMP_BAUD} baud, format "
               "${C62_BASEBAND_DUMP_FORMAT}, burst ${C62_BASEBAND_DUMP_BURST})")

add_subdirectory(services)
zephyr_library_link_libraries(lsf_client)
zephyr_linker_sources(DATA_SECTIONS lsf_service.ld)

target_include_directories(app
  PRIVATE
    ${OPENRTX_ROOT}/platform/targets/c62
)

target_compile_definitions(app PRIVATE PLATFORM_C62)

#
# DSP firmware, flashed separately at 0x100000 (dsp_firmware partition). The
# firmware does not boot without it, and it must match the audio code, so the
# build pins one image by hash: by default it is downloaded from OpenRTX's
# lsf-zephyr-sdk, or C62_DSP_FIRMWARE (CMake or environment variable) gives a
# local copy for offline builds. Either way it ends up next to zephyr.bin.
#
set(C62_DSP_FIRMWARE_URL
    "https://raw.githubusercontent.com/OpenRTX/lsf-zephyr-sdk/2ac605527dcde3e90c814bb4175546cc7fbd36da/resources/dsp_firmware.bin")
set(C62_DSP_FIRMWARE_SHA256
    "9579b617f048c646465118f6d0b9e1b765b4d7d811ff9c8649c5754fe45a8903")
set(C62_DSP_FIRMWARE_OUT ${CMAKE_BINARY_DIR}/zephyr/dsp_firmware.bin)

if(NOT DEFINED C62_DSP_FIRMWARE AND DEFINED ENV{C62_DSP_FIRMWARE})
  set(C62_DSP_FIRMWARE $ENV{C62_DSP_FIRMWARE})
endif()

# A relative path is from the OpenRTX root. Check the hash here, as a
# mismatch in file(DOWNLOAD) stops with CMake's own error.
if(C62_DSP_FIRMWARE)
  get_filename_component(c62_dsp_local "${C62_DSP_FIRMWARE}" ABSOLUTE
                         BASE_DIR "${OPENRTX_ROOT}")
  if(EXISTS "${c62_dsp_local}")
    file(SHA256 "${c62_dsp_local}" c62_dsp_local_sha256)
  endif()
  if(NOT "${c62_dsp_local_sha256}" STREQUAL "${C62_DSP_FIRMWARE_SHA256}")
    message(FATAL_ERROR "C62_DSP_FIRMWARE ${c62_dsp_local} is missing or is "
                        "not the pinned DSP firmware (sha256 "
                        "${C62_DSP_FIRMWARE_SHA256}).")
  endif()
  configure_file("${c62_dsp_local}" "${C62_DSP_FIRMWARE_OUT}" COPYONLY)
else()
  # Skipped when the file is already there with the expected hash
  file(DOWNLOAD "${C62_DSP_FIRMWARE_URL}" "${C62_DSP_FIRMWARE_OUT}"
       EXPECTED_HASH SHA256=${C62_DSP_FIRMWARE_SHA256}
       TLS_VERIFY ON
       STATUS c62_dsp_status)
  list(GET c62_dsp_status 0 c62_dsp_error)
  if(c62_dsp_error)
    list(GET c62_dsp_status 1 c62_dsp_message)
    message(FATAL_ERROR "Could not get the C62 DSP firmware from "
                        "${C62_DSP_FIRMWARE_URL} (${c62_dsp_message}). Set "
                        "C62_DSP_FIRMWARE to a local copy with sha256 "
                        "${C62_DSP_FIRMWARE_SHA256}.")
  endif()
endif()

message(STATUS "C62 DSP firmware: ${C62_DSP_FIRMWARE_OUT}, flash at 0x100000")
