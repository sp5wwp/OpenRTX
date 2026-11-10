/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ARCH_REGISTERS_IMPL_H
#define ARCH_REGISTERS_IMPL_H

#include "stm32u5xx.h"
#include "system_stm32u5xx.h"

#define RCC_SYNC() __DSB()

#endif // ARCH_REGISTERS_IMPL_H
