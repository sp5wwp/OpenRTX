# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later

# The radio is normally flashed with the vendor update tool through the .BTF
# image, these runners are only useful with an SWD probe soldered to the board.
board_runner_args(pyocd "--target=AT32F403AVGT7")
board_runner_args(jlink "--device=AT32F403AVGT7" "--speed=4000")

include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
