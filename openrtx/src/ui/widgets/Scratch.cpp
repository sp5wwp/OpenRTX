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

alignas(Scratch::ALIGN) uint8_t Scratch::storage_[Scratch::SIZE];
const void *Scratch::type_ = nullptr;
void (*Scratch::destroy_)(void *p) = nullptr;

void Scratch::release()
{
    if (destroy_ != nullptr)
        destroy_(storage_);

    type_ = nullptr;
    destroy_ = nullptr;
}

} // namespace ortxui
