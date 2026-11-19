/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "views/ViewSlot.hpp"
#include "core/graphics.h"
#include "core/state.h"
#include "core/utils.h"

#include <new>

namespace ortxui
{

constexpr size_t ViewSlot::SIZE;
constexpr size_t ViewSlot::ALIGN;

/**
 * Construct a view in the slot storage.
 *
 * @param mem: slot storage.
 * @return the view.
 */
template <class T>
static View *make(void *mem)
{
    static_assert(sizeof(T) <= ViewSlot::SIZE, "view larger than the slot");
    static_assert(alignof(T) <= ViewSlot::ALIGN, "view alignment too strict");
    return new (mem) T();
}

/** How to construct each screen, indexed by ViewId; nullptr when the target
 * lacks the feature. */
static View *(*const BUILDERS[])(void *) = {
    make<VfoView>,      // ViewId::Vfo
    make<MenuView>,     // ViewId::MainMenu
    make<MenuView>,     // ViewId::SettingsMenu
    make<MacroView>,    // ViewId::Macro
    make<InfoView>,     // ViewId::Info
    make<AboutView>,    // ViewId::About
    make<DisplayView>,  // ViewId::Display
#ifdef CONFIG_RTC
    make<TimeDateView>, // ViewId::TimeDate
#else
    nullptr,
#endif
    make<ChecklistView>, // ViewId::Accessibility
    make<FmView>,        // ViewId::Fm
    make<RadioView>,     // ViewId::Radio
#ifdef CONFIG_M17
    make<M17View>,       // ViewId::M17
#else
    nullptr,
#endif
    make<DefaultsView>,    // ViewId::Defaults
    make<ChannelsView>,    // ViewId::Channels
    make<ContactsView>,    // ViewId::Contacts
    make<BanksView>,       // ViewId::Banks
    make<TextInputView>,   // ViewId::TextInput
    make<PickerView>,      // ViewId::Picker
#ifdef CONFIG_GPS
    make<GpsView>,         // ViewId::Gps
    make<GpsSettingsView>, // ViewId::GpsSettings
#else
    nullptr,
    nullptr,
#endif
#ifdef CONFIG_MESSAGES
    make<MessagesView>,       // ViewId::Messages
    make<MessageDetailView>,  // ViewId::MessageDetail
    make<MessageComposeView>, // ViewId::MessageCompose
#else
    nullptr,
    nullptr,
    nullptr,
#endif
};

static_assert(ARRAY_SIZE(BUILDERS) == VIEW_COUNT, "a screen has no builder");

ViewSlot::ViewSlot() : view_(nullptr)
{
}

ViewSlot::~ViewSlot()
{
    destroy();
}

View *ViewSlot::build(ViewId id)
{
    destroy();
    view_ = BUILDERS[static_cast<uint8_t>(id)](storage_);
    view_->id_ = id;

    /* Lay out with the smooth font metrics, as every view did when they were
     * all built at start-up, before the crisp text setting was applied. */
    gfx_setFontMono(false);
    view_->build();
    gfx_setFontMono(state.settings.crispText);
    return view_;
}

void ViewSlot::destroy()
{
    if (view_ == nullptr)
        return;

    view_->~View();
    view_ = nullptr;
}

} // namespace ortxui
