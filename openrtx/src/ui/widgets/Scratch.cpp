/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "widgets/Scratch.hpp"

namespace ortxui
{

constexpr size_t Scratch::SIZE;
constexpr size_t Scratch::ALIGN;

alignas(Scratch::ALIGN) uint8_t Scratch::storage[Scratch::SIZE];
const void *Scratch::type = nullptr;
void (*Scratch::destroy)(void *p) = nullptr;

void Scratch::release()
{
    if (destroy != nullptr)
        destroy(storage);

    type = nullptr;
    destroy = nullptr;
}

} // namespace ortxui
