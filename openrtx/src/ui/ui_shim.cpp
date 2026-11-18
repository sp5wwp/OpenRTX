/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * ui_shim.cpp -- the C ABI boundary of the "ortx" UI toolkit.
 *
 * Implements the eight functions of core/ui.h (the frozen seam the rest of the
 * firmware calls, only from threads.c / openrtx.c / state.c) and forwards into
 * the C++ retained-widget toolkit. threads.c, graphics.c, the state module,
 * the event queue and the sync_rtx/mutex discipline are all unchanged.
 *
 * The active screen is the top of a Navigator's View stack (each View driving a
 * Screen). Input events are decoded and dispatched into the active view, which
 * returns a navigation intent (push a child screen / pop back); drawing pulls
 * from the state snapshot and repaints only when something actually changed.
 * The VFO home, a main menu and a settings submenu exist so far; remaining
 * views land in later slices.
 */

#include "core/ui.h"
#include "core/state.h"
#include "core/event.h"
#include "core/input.h"
#include "core/graphics.h"
#include "interfaces/delays.h"
#include "interfaces/display.h"
#include "interfaces/keyboard.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"
#include "rtx/rtx.h"
#include "hwconfig.h"

#include "core/Event.hpp"
#include "core/Navigator.hpp"
#include "render/DrawCtx.hpp"
#include "style/SemanticColor.hpp"
#include "views/VfoView.hpp"
#include "views/MacroView.hpp"
#include "views/VolumeOverlayView.hpp"
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

#include <cstdint>

using namespace ortxui;

namespace
{

/* Classic UI event ring: single-thread producer/consumer (all in the UI
 * thread), so no locking is required. */
constexpr uint8_t MAX_NUM_EVENTS = 16;

event_t evQueue[MAX_NUM_EVENTS];
uint8_t evQueue_rdPos = 0;
uint8_t evQueue_wrPos = 0;

/* UI-thread-local snapshot of the radio state. ui_saveState() copies the
 * global `state` here under state_mutex; drawing reads only this snapshot. */
state_t last_state;

/* Menu item tables, also used to wire the rows to their views. Content
 * mirrors the classic UI's menus; the config guards keep parity with targets
 * that omit GPS/RTC/M17. */
const UiText kMainMenu[] = {
    STR(banks),    STR(channels), STR(contacts),
#ifdef CONFIG_GPS
    STR(gps),
#endif
#ifdef CONFIG_MESSAGES
    STR(messages),
#endif
    STR(settings), STR(info),     STR(about),
};
constexpr uint16_t kMainMenuCount = sizeof(kMainMenu) / sizeof(kMainMenu[0]);

const UiText kSettingsMenu[] = {
    STR(display),
#ifdef CONFIG_RTC
    STR(timeAndDate),
#endif
#ifdef CONFIG_GPS
    STR(gps),
#endif
    STR(radio),
#ifdef CONFIG_M17
    STR(m17),
#endif
    STR(fm),          STR(accessibility), STR(defaultSettings),
};
constexpr uint16_t kSettingsMenuCount = sizeof(kSettingsMenu)
                                      / sizeof(kSettingsMenu[0]);

/* The view singletons and the Navigator whose stack top is the active screen.
 * Each View owns its widget tree in static storage. */
VfoView vfoView;
MacroView macroView;
VolumeOverlayView volumeOverlay;
MenuView mainMenu;
MenuView settingsMenu;
InfoView infoView;
AboutView aboutView;
DisplayView displayView;
#ifdef CONFIG_RTC
TimeDateView timeDateView;
#endif
ChecklistView checklistView;
FmView fmView;
RadioView radioView;
DefaultsView defaultsView;
ChannelsView channelsView;
ContactsView contactsView;
BanksView banksView;
TextInputView textInputView;
PickerView pickerView;
#ifdef CONFIG_M17
M17View m17View;
#endif
#ifdef CONFIG_GPS
GpsView gpsView;
GpsSettingsView gpsSettingsView;
#endif
#ifdef CONFIG_MESSAGES
MessagesView messagesView;
MessageDetailView messageDetailView;
MessageComposeView messageComposeView;
#endif
Navigator nav;

/**
 * Wire the row of a menu whose label matches a given one to a view.
 *
 * @param menu: menu to wire.
 * @param table: the menu's row labels.
 * @param count: number of rows.
 * @param label: label of the row to wire.
 * @param target: view the row opens.
 */
void wireRow(MenuView &menu, const UiText *table, uint16_t count,
             const UiText &label, View *target)
{
    for (uint16_t i = 0; i < count; i++) {
        if (table[i] == label) {
            menu.setRowTarget(i, target);
            return;
        }
    }
}

/* Display standby (backlight timeout) — parity with the classic default UI.
 * On `settings.display_timer` of idle the backlight is blanked and drawing is
 * suspended; any keypress (or RF/volume activity) wakes it again. */
bool standby = false;
long long last_event_tick = 0;

/* Macro menu overlay (shown while MONI is held; long-press latches it open when
 * settings.macroMenuLatch is set). Managed here because it is triggered by a
 * global held key, not a view's own navigation intent. */
bool macroOpen = false;
bool macroLatched = false;

/* Transient volume HUD: shown while the user turns the physical volume knob,
 * auto-dismissed this many ms after the level stops changing. Driven here (not
 * by a view) because the knob is a polled analogue value, not an input event. */
constexpr uint32_t kVolumeOverlayMs = 500;

void enterStandby()
{
    if (standby)
        return;
    standby = true;
    display_setBacklightLevel(0);
}

/* Returns true if this call actually left standby (used to swallow the wake
 * keypress, matching the classic behaviour). */
bool exitStandby(long long now)
{
    last_event_tick = now;
    if (!standby)
        return false;
    standby = false;
    display_setBacklightLevel(state.settings.brightness);
    if (View *v = nav.active())
        v->screen().markAllDirty(); /* repaint fully on wake */
    return true;
}

} // namespace

/* Pure idle-timeout predicate — C linkage so the unit test (ui_check_standby)
 * links against it exactly as it did the classic implementation. Mirrors the
 * classic thresholds verbatim. */
extern "C" bool _ui_checkStandby(long long time_since_last_event)
{
    if (standby)
        return false;

    switch (state.settings.display_timer) {
        case TIMER_OFF:
            return false;
        case TIMER_5S:
        case TIMER_10S:
        case TIMER_15S:
        case TIMER_20S:
        case TIMER_25S:
        case TIMER_30S:
            return time_since_last_event
                >= (5000 * state.settings.display_timer);
        case TIMER_1M:
        case TIMER_2M:
        case TIMER_3M:
        case TIMER_4M:
        case TIMER_5M:
            return time_since_last_event
                >= (60000 * (state.settings.display_timer - (TIMER_1M - 1)));
        case TIMER_15M:
        case TIMER_30M:
        case TIMER_45M:
            return time_since_last_event
                >= (60000 * 15
                    * (state.settings.display_timer - (TIMER_15M - 1)));
        case TIMER_1H:
            return time_since_last_event >= 60 * 60 * 1000;
    }

    return false;
}

extern "C" void ui_init()
{
    evQueue_rdPos = 0;
    evQueue_wrPos = 0;
    last_state = state;

    vfoView.build();
    macroView.build();
    volumeOverlay.build();
    mainMenu.build(STR(menu), kMainMenu, kMainMenuCount);
    settingsMenu.build(STR(settings), kSettingsMenu, kSettingsMenuCount);
    infoView.build();
    aboutView.build();
    displayView.build();
#ifdef CONFIG_RTC
    timeDateView.build();
#endif
    checklistView.build();
    fmView.build();
    radioView.build();
    defaultsView.build();
    channelsView.build();
    contactsView.build();
    banksView.build();
    textInputView.build();
    pickerView.build();
    textInputView.setPicker(&pickerView);
#ifdef CONFIG_M17
    m17View.build();
    m17View.setTextEditor(&textInputView);
#endif
#ifdef CONFIG_GPS
    gpsView.build();
    gpsSettingsView.build();
#endif
#ifdef CONFIG_MESSAGES
    messagesView.build();
    messageDetailView.build();
    messageComposeView.build();
    messagesView.setViews(&messageDetailView, &messageComposeView);
    messageDetailView.setComposer(&messageComposeView);
    messageComposeView.setTextEditor(&textInputView);
#endif

    /* Wire menu rows to their views. Rows resolve by label because the indices
     * shift with the config guards on the menu tables. */
    wireRow(mainMenu, kMainMenu, kMainMenuCount, STR(banks), &banksView);
    wireRow(mainMenu, kMainMenu, kMainMenuCount, STR(channels), &channelsView);
    wireRow(mainMenu, kMainMenu, kMainMenuCount, STR(contacts), &contactsView);
    wireRow(mainMenu, kMainMenu, kMainMenuCount, STR(settings), &settingsMenu);
    wireRow(mainMenu, kMainMenu, kMainMenuCount, STR(info), &infoView);
    wireRow(mainMenu, kMainMenu, kMainMenuCount, STR(about), &aboutView);
    wireRow(settingsMenu, kSettingsMenu, kSettingsMenuCount, STR(display),
            &displayView);
    wireRow(settingsMenu, kSettingsMenu, kSettingsMenuCount, STR(accessibility),
            &checklistView);
    wireRow(settingsMenu, kSettingsMenu, kSettingsMenuCount, STR(fm), &fmView);
    wireRow(settingsMenu, kSettingsMenu, kSettingsMenuCount, STR(radio),
            &radioView);
    wireRow(settingsMenu, kSettingsMenu, kSettingsMenuCount,
            STR(defaultSettings), &defaultsView);
#ifdef CONFIG_RTC
    wireRow(settingsMenu, kSettingsMenu, kSettingsMenuCount, STR(timeAndDate),
            &timeDateView);
#endif
#ifdef CONFIG_M17
    wireRow(settingsMenu, kSettingsMenu, kSettingsMenuCount, STR(m17),
            &m17View);
#endif
#ifdef CONFIG_GPS
    wireRow(mainMenu, kMainMenu, kMainMenuCount, STR(gps), &gpsView);
    wireRow(settingsMenu, kSettingsMenu, kSettingsMenuCount, STR(gps),
            &gpsSettingsView);
#endif
#ifdef CONFIG_MESSAGES
    wireRow(mainMenu, kMainMenu, kMainMenuCount, STR(messages), &messagesView);
#endif

    vfoView.setMenu(&mainMenu);
    nav.setRoot(&vfoView);

    /* Apply the persisted text-rendering setting (crisp/1bpp vs smooth/AA). The
     * theme (Instrument vs High Contrast) needs no apply — themeColor() reads it
     * live. Both are no-ops on 1bpp targets. */
    gfx_setFontMono(state.settings.crispText);

    standby = false;
    last_event_tick = getTick();

    state.ui_screen = 0; /* MAIN_VFO */
}

extern "C" void ui_drawSplashScreen()
{
    DrawCtx d;
    d.clearScreen(Sem::Background);

    const Point logo = { 0, (int16_t)((CONFIG_SCREEN_HEIGHT / 2) - 6) };
    const Point call = { 0, (int16_t)(CONFIG_SCREEN_HEIGHT - 8) };
    /* Render the splash 1bpp (no anti-aliasing) in the gold brand accent, to
     * match the crisp classic boot logo. Restore the user's text setting after
     * (this also establishes its resting value for the first UI frame). */
    gfx_setFontMono(true);
    d.text(logo, FONT_SIZE_12PT, TEXT_ALIGN_CENTER, Sem::Accent, "OPN\nRTX");
    d.text(call, FONT_SIZE_8PT, TEXT_ALIGN_CENTER, Sem::OnSurface,
           state.settings.callsign);
    gfx_setFontMono(state.settings.crispText);

    vp_announceSplashScreen();
}

extern "C" void ui_saveState()
{
    last_state = state;
}

extern "C" void ui_updateFSM(bool *sync_rtx)
{
    const long long now = getTick();

    /* Wake-keeping activity — ongoing RF (TX or an open squelch) or the user
     * turning the volume — keeps the display awake and wakes it from standby.
     * Checked every tick, BEFORE draining the event queue: state_task bundles
     * the volume update with an EVENT_STATUS, so gating this on an empty queue
     * would let that status event mask the change and the volume-wake would
     * never fire. exitStandby also refreshes the idle timer. */
    if ((rtx_getStatus()->opStatus == TX) || rtx_rxSquelchOpen()
        || (state.volume != last_state.volume))
        exitStandby(now);

    /* Volume HUD: turning the knob (a change in the polled level) presents a
     * transient level bar and re-pokes it while it keeps moving; it auto-hides
     * kVolumeOverlayMs after the level settles. Suppressed while the macro menu
     * is open so the two modals never fight. The FM squelch deliberately is not
     * shown here — it stays in the macro menu, reusing the same BarRow control.
     * Checked before draining the queue, like the wake-keeping block: the
     * volume update rides a periodic EVENT_STATUS, so gating on an empty queue
     * would let that event mask the change. */
    if ((state.volume != last_state.volume) && !macroOpen)
        nav.presentOverlay(&volumeOverlay, OverlayDismiss::Transient, now,
                           kVolumeOverlayMs);

    /* Apply managed-overlay dismiss policies every tick (unconditionally, for
     * the same empty-queue reason as above). */
    nav.tickOverlay(now);

    if (evQueue_rdPos != evQueue_wrPos) {
        /* Pop one event per tick, matching the classic loop cadence. */
        const event_t raw = evQueue[evQueue_rdPos];
        evQueue_rdPos = (uint8_t)((evQueue_rdPos + 1) % MAX_NUM_EVENTS);

        /* A keypress always refreshes the idle timer; if it also woke the
         * display we consume it (except MONI, kept for the macro menu) so the
         * wake press isn't acted on — parity with the classic UI. */
        if (raw.type == EVENT_KBD) {
            kbd_msg_t msg;
            msg.value = raw.payload;
            const bool woke = exitStandby(now);
            if (woke && ((msg.keys & KEY_MONI) == 0u))
                return;

            /* Any real input (key or tuning knob, both EVENT_KBD) dismisses the
             * transient volume HUD, so the event acts on the screen underneath
             * instead of being swallowed by the overlay. */
            if (nav.overlay() == &volumeOverlay)
                nav.dismissOverlay();

            /* F1 replays the last voice prompt (global, matches classic). */
            if (((msg.keys & KEY_F1) != 0u)
                && (state.settings.vpLevel > vpBeep)) {
                vp_replayLastPrompt();
                return;
            }

            /* Keypad lock: while locked, ignore keys unless the macro menu is
             * open or MONI is pressed (so it can always be opened to unlock). */
            if (state.keypad_locked && !macroOpen
                && ((msg.keys & KEY_MONI) == 0u))
                return;

            /* Macro menu: shown while MONI is held. A long-press latches it open
             * (if enabled); a MONI press while latched, or releasing MONI when
             * not latched, closes it. */
            const bool moni = ((msg.keys & KEY_MONI) != 0u);
            if (moni) {
                if (!macroOpen) {
                    nav.openOverlay(&macroView);
                    macroOpen = true;
                    macroLatched = false;
                } else if (macroLatched) {
                    nav.closeOverlay();
                    macroOpen = false;
                    macroLatched = false;
                    return;
                }
                if (msg.long_press && (state.settings.macroMenuLatch != 0u))
                    macroLatched = true;
            } else if (macroOpen && !macroLatched) {
                nav.closeOverlay();
                macroOpen = false;
                return; /* consume the closing (key-release) event */
            }
        }

        /* The view that handles the event is the active one at dispatch time; if
         * it edited an rtx-affecting field it flags a resync, which we forward so
         * threads.c re-applies state.channel to the radio. */
        View *handler = nav.active();
        const Event ev = Event::decode((uint8_t)raw.type, raw.payload);
        nav.dispatch(ev);

        if ((handler != nullptr) && handler->takeSyncRtx())
            *sync_rtx = true;
        return;
    }

    /* No event this tick: blank the backlight once the idle timer elapses
     * (the wake-keeping activity was already handled at the top). */
    if (_ui_checkStandby(now - last_event_tick))
        enterStandby();
}

extern "C" bool ui_updateGUI()
{
    if (standby)
        return false; /* backlight off — nothing to draw */

    View *view = nav.active();
    if (view == nullptr)
        return false;

    view->syncFromState(last_state);

    Screen &screen = view->screen();
    if (!screen.dirty())
        return false;

    DrawCtx d;
    d.clearScreen(Sem::Background);
    screen.paintTree(d);
    screen.clearDirty();
    return true;
}

extern "C" bool ui_pushEvent(const uint8_t type, const uint32_t data)
{
    const uint8_t newWrPos = (uint8_t)((evQueue_wrPos + 1) % MAX_NUM_EVENTS);
    if (newWrPos == evQueue_rdPos)
        return false; /* queue full */

    event_t event;
    event.value = 0;
    event.type = type;
    event.payload = data;
    evQueue[evQueue_wrPos] = event;
    evQueue_wrPos = newWrPos;
    return true;
}

extern "C" void ui_terminate()
{
}
