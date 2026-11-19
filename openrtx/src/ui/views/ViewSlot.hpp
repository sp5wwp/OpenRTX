/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORTX_UI_VIEWSLOT_HPP
#define ORTX_UI_VIEWSLOT_HPP

#ifndef __cplusplus
#error This header is C++ only!
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include "core/ViewId.hpp"
#include "views/VfoView.hpp"
#include "views/MacroView.hpp"
#include "views/MenuView.hpp"
#include "views/InfoView.hpp"
#include "views/AboutView.hpp"
#include "views/DisplayView.hpp"
#include "views/TimeDateView.hpp"
#include "views/ChecklistView.hpp"
#include "views/GpsView.hpp"
#include "views/GpsSettingsView.hpp"
#include "views/FmView.hpp"
#include "views/RadioView.hpp"
#include "views/M17View.hpp"
#include "views/DefaultsView.hpp"
#include "views/ChannelsView.hpp"
#include "views/ContactsView.hpp"
#include "views/BanksView.hpp"
#include "views/TextInputView.hpp"
#include "views/PickerView.hpp"
#include "views/MessagesView.hpp"
#include "views/MessageDetailView.hpp"
#include "views/MessageComposeView.hpp"

namespace ortxui
{

/**
 * Storage for the one live View. A screen is built in place from its ViewId
 * and destroyed when another replaces it, so the views cost the RAM of the
 * largest one instead of the sum of all of them.
 */
class ViewSlot
{
public:
    /** Bytes of the largest view. */
    static constexpr size_t SIZE = std::max({
        sizeof(VfoView),
        sizeof(MacroView),
        sizeof(MenuView),
        sizeof(InfoView),
        sizeof(AboutView),
        sizeof(DisplayView),
#ifdef CONFIG_RTC
        sizeof(TimeDateView),
#endif
        sizeof(ChecklistView),
        sizeof(FmView),
        sizeof(RadioView),
#ifdef CONFIG_M17
        sizeof(M17View),
#endif
        sizeof(DefaultsView),
        sizeof(ChannelsView),
        sizeof(ContactsView),
        sizeof(BanksView),
        sizeof(TextInputView),
        sizeof(PickerView),
#ifdef CONFIG_GPS
        sizeof(GpsView),
        sizeof(GpsSettingsView),
#endif
#ifdef CONFIG_MESSAGES
        sizeof(MessagesView),
        sizeof(MessageDetailView),
        sizeof(MessageComposeView),
#endif
    });

    /** Strictest alignment of the views. */
    static constexpr size_t ALIGN = std::max({
        alignof(VfoView),
        alignof(MacroView),
        alignof(MenuView),
        alignof(TextInputView),
        alignof(View),
    });

    /** Constructor. */
    ViewSlot();

    /** Destructor. */
    ~ViewSlot();

    /**
     * Destroy the current view and build a screen in its place.
     *
     * @param id: screen to build.
     * @return the new view.
     */
    View *build(ViewId id);

    /** Destroy the current view, if any. */
    void destroy();

    /**
     * Get the current view.
     *
     * @return the view, or nullptr if the slot is empty.
     */
    View *view() const
    {
        return view_;
    }

private:
    alignas(ALIGN) uint8_t storage_[SIZE]; ///< Where the view is built
    View *view_;                           ///< Current view, or nullptr
};

} // namespace ortxui

#endif /* ORTX_UI_VIEWSLOT_HPP */
