/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "app/ViewSlot.hpp"
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

/**
 * How to construct a screen.
 */
struct Builder {
    ViewId id;             ///< Screen
    View *(*make)(void *); ///< Constructor, nullptr if the target lacks it
};

/** How to construct each screen, in ViewId order. */
static constexpr Builder BUILDERS[] = {
    { ViewId::Vfo, make<VfoView> },
    { ViewId::MainMenu, make<MenuView> },
    { ViewId::SettingsMenu, make<MenuView> },
    { ViewId::Macro, make<MacroView> },
    { ViewId::Info, make<InfoView> },
    { ViewId::About, make<AboutView> },
    { ViewId::Display, make<DisplayView> },
#ifdef CONFIG_RTC
    { ViewId::TimeDate, make<TimeDateView> },
#else
    { ViewId::TimeDate, nullptr },
#endif
    { ViewId::Accessibility, make<ChecklistView> },
    { ViewId::Fm, make<FmView> },
    { ViewId::Radio, make<RadioView> },
#ifdef CONFIG_M17
    { ViewId::M17, make<M17View> },
#else
    { ViewId::M17, nullptr },
#endif
    { ViewId::Defaults, make<DefaultsView> },
    { ViewId::Channels, make<ChannelsView> },
    { ViewId::Contacts, make<ContactsView> },
    { ViewId::Banks, make<BanksView> },
    { ViewId::TextInput, make<TextInputView> },
    { ViewId::Picker, make<PickerView> },
#ifdef CONFIG_GPS
    { ViewId::Gps, make<GpsView> },
    { ViewId::GpsSettings, make<GpsSettingsView> },
#else
    { ViewId::Gps, nullptr },
    { ViewId::GpsSettings, nullptr },
#endif
#ifdef CONFIG_MESSAGES
    { ViewId::Messages, make<MessagesView> },
    { ViewId::MessageDetail, make<MessageDetailView> },
    { ViewId::MessageCompose, make<MessageComposeView> },
#else
    { ViewId::Messages, nullptr },
    { ViewId::MessageDetail, nullptr },
    { ViewId::MessageCompose, nullptr },
#endif
};

/**
 * Check that each screen's builder sits at its ViewId.
 *
 * @return true if every entry is in place.
 */
static constexpr bool buildersInOrder()
{
    for (uint8_t i = 0; i < ARRAY_SIZE(BUILDERS); i++) {
        if (static_cast<uint8_t>(BUILDERS[i].id) != i)
            return false;
    }

    return true;
}

static_assert(ARRAY_SIZE(BUILDERS) == VIEW_COUNT, "a screen has no builder");
static_assert(buildersInOrder(), "a builder is not at its ViewId");

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
    view_ = BUILDERS[static_cast<uint8_t>(id)].make(storage_);
    view_->id_ = id;
    view_->build();
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
