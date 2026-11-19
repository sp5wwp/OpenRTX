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
 * The active screen is the top of a Navigator's screen stack; only that view
 * exists, built in a ViewSlot, with what must outlive it kept in UiMemory.
 * Input events are decoded and dispatched into the active view, which returns
 * a navigation intent (push a child screen / pop back); drawing pulls from the
 * state snapshot and repaints only when something actually changed, then sends
 * the screen to the display.
 */

#include "core/ui.h"
#include "core/beeps.h"
#include "core/state.h"
#include "core/event.h"
#include "core/input.h"
#include "core/graphics.h"
#include "interfaces/delays.h"
#include "interfaces/display.h"
#include "interfaces/keyboard.h"
#include "interfaces/platform.h"
#include "core/voicePrompts.h"
#include "core/voicePromptUtils.h"
#include "rtx/rtx.h"
#include "hwconfig.h"

#include "core/Event.hpp"
#include "core/Navigator.hpp"
#include "core/UiMemory.hpp"
#include "render/DrawCtx.hpp"
#include "style/SemanticColor.hpp"
#include "views/LowBatteryScreen.hpp"
#include "views/VfoView.hpp"
#include "views/ViewSlot.hpp"
#include "views/VolumeOverlayView.hpp"

#include <cstdint>

using namespace ortxui;

/* Event ring: pushed and popped with state_mutex held. */
static constexpr uint8_t MAX_NUM_EVENTS = 16;

static event_t evQueue[MAX_NUM_EVENTS];
static uint8_t evQueue_rdPos = 0;
static uint8_t evQueue_wrPos = 0;

/* UI-thread-local snapshot of the radio state. ui_saveState() copies the
 * global `state` here under state_mutex; drawing reads only this snapshot. */
static state_t last_state;

/* The one live view, the screen stack over it, and the volume HUD, which
 * stays built because it draws over the screen without replacing it. */
static ViewSlot viewSlot;
static Navigator nav(viewSlot, uiMemory);
static VolumeOverlayView volumeOverlay;

/* Display standby (backlight timeout) — parity with the classic default UI.
 * On `settings.display_timer` of idle the backlight is blanked and drawing is
 * suspended; any keypress (or RF/volume activity) wakes it again. */
static bool standby = false;
static long long last_event_tick = 0;

/* Macro menu overlay (shown while MONI is held; long-press latches it open when
 * settings.macroMenuLatch is set). Managed here because it is triggered by a
 * global held key, not a view's own navigation intent. */
static bool macroOpen = false;
static bool macroLatched = false;

#if !defined(PLATFORM_TTWRPLUS)
/* The low-battery warning, drawn instead of the active screen while shown. */
static LowBatteryScreen lowBattery;
static bool lowBatteryShown = false;
#endif

/* Transient volume HUD: shown while the user turns the physical volume knob,
 * auto-dismissed this many ms after the level stops changing. Driven here (not
 * by a view) because the knob is a polled analogue value, not an input event. */
static constexpr uint32_t kVolumeOverlayMs = 500;

/* Keys that click at the Beep voice level: the keypad, ENTER, ESC, UP and
 * DOWN. */
static constexpr uint32_t CLICK_KEYS = KBD_CHAR_MASK | KEY_ENTER | KEY_ESC
                                     | KEY_UP | KEY_DOWN;

/**
 * Draw the whole screen and send it to the display: in one go, or a band at a
 * time on targets drawing in bands, each band drawing everything and keeping
 * what lands in it.
 *
 * @param paint: draws the screen content with the DrawCtx it is given.
 */
template <typename Paint>
static void drawScreen(Paint paint)
{
    for (uint16_t y = 0; y < CONFIG_SCREEN_HEIGHT; y += GFX_BAND_ROWS) {
        gfx_beginBand(static_cast<uint8_t>(y));
        const Rect band = { 0, static_cast<int16_t>(y), CONFIG_SCREEN_WIDTH,
                            GFX_BAND_ROWS };
        DrawCtx d(band);
        d.clearScreen(Sem::Background);
        paint(d);
        gfx_render();
    }
}

/**
 * Blank the backlight and stop drawing until the next wake-up.
 */
static void enterStandby()
{
    if (standby)
        return;
    standby = true;
    display_setBacklightLevel(0);
}

/**
 * Refresh the idle timer and leave standby if in it.
 *
 * @param now: current tick.
 * @return true if this call left standby, so the wake keypress is swallowed
 * (classic behaviour).
 */
static bool exitStandby(long long now)
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

    volumeOverlay.build();
    nav.setRoot(ViewId::Vfo);

    /* Apply the persisted text-rendering setting (crisp/1bpp vs smooth/AA). The
     * theme (Instrument vs High Contrast) needs no apply — themeColor() reads it
     * live. Both are no-ops on 1bpp targets. */
    gfx_setFontMono(state.settings.crispText);

    standby = false;
    last_event_tick = getTick();
    macroOpen = false;
    macroLatched = false;
#if !defined(PLATFORM_TTWRPLUS)
    lowBatteryShown = false;
#endif
}

extern "C" void ui_drawSplashScreen()
{
    const Point logo = { 0, (int16_t)((CONFIG_SCREEN_HEIGHT / 2) - 6) };
    const Point call = { 0, (int16_t)(CONFIG_SCREEN_HEIGHT - 8) };
    /* Render the splash 1bpp (no anti-aliasing) in the gold brand accent, to
     * match the crisp classic boot logo. Restore the user's text setting after
     * (this also establishes its resting value for the first UI frame). */
    gfx_setFontMono(true);
    drawScreen([&](DrawCtx &d) {
        d.text(logo, FONT_SIZE_12PT, TEXT_ALIGN_CENTER, Sem::Accent,
               "OPN\nRTX");
        d.text(call, FONT_SIZE_8PT, TEXT_ALIGN_CENTER, Sem::OnSurface,
               state.settings.callsign);
    });
    gfx_setFontMono(state.settings.crispText);

    vp_announceSplashScreen();
}

extern "C" void ui_saveState()
{
    last_state = state;
}

/**
 * Close the macro menu and show the screen below it again.
 */
static void closeMacro()
{
    nav.closeOverlay();
    macroOpen = false;
    macroLatched = false;
}

#if !defined(PLATFORM_TTWRPLUS)
/**
 * Show the low-battery warning while the battery is empty and no emergency
 * use has been declared. Not while transmitting, as the PA load drops the
 * battery voltage.
 */
static void updateLowBattery()
{
    const bool empty = !state.emergency && !platform_getPttStatus()
                    && (state.charge == 0u);
    if (empty == lowBatteryShown)
        return;

    lowBatteryShown = empty;
    if (empty)
        lowBattery.markAllDirty();
    else if (View *v = nav.active())
        v->screen().markAllDirty();
}

/**
 * Handle an event while the low-battery warning shows: a key press declares
 * emergency use and goes back to the home screen.
 *
 * @param msg: key event.
 * @param now: current tick.
 */
static void lowBatteryKey(const kbd_msg_t &msg, long long now)
{
    if (msg.keys == 0u)
        return;

    exitStandby(now);
    state.emergency = true;
    lowBatteryShown = false;
    macroOpen = false;
    macroLatched = false;
    nav.setRoot(ViewId::Vfo);
}
#endif

/**
 * Check whether a key event keys or ends the FM 1750 Hz tone burst on the
 * home screen, which the keypad lock lets through.
 *
 * @param msg: key event.
 * @return true for the # press and the events while the tone plays.
 */
static bool isToneBurstKey(const kbd_msg_t &msg)
{
    if ((nav.activeId() != ViewId::Vfo) || (state.channel.mode != OPMODE_FM))
        return false;

    return (msg.keys == KEY_HASH) || state.tone_enabled;
}

/**
 * Check whether the home screen is shown with no menu or entry open on it.
 *
 * @return true if PTT may key up and F1 speaks the channel.
 */
static bool atHome()
{
    if (macroOpen || (nav.activeId() != ViewId::Vfo))
        return false;

    /* The slot holds the home screen while it tops the stack. */
    const VfoView *vfo = static_cast<const VfoView *>(viewSlot.view());
    return !vfo->freqEntryOpen();
}

/**
 * Speak on F1: a press replays the last prompt, a long press speaks the
 * channel summary on the home screen or the GPS status on the GPS screen.
 *
 * @param msg: F1 key event.
 */
static void speakF1(const kbd_msg_t &msg)
{
    if (msg.repeat)
        return;

    if (msg.long_press && atHome()) {
        const uint16_t index = (state.tuner_mode == VFO) ?
                                   0 :
                                   (state.channel_index + 1);
        vp_announceChannelSummary(&state.channel, index, state.bank, vpAllInfo);
        return;
    }

#ifdef CONFIG_GPS
    if (msg.long_press && (nav.activeId() == ViewId::Gps)) {
        vp_announceGPSInfo(vpGPSAll);
        return;
    }
#endif

    vp_replayLastPrompt();
}

/**
 * Click a key press at the Beep voice level, where nothing else speaks it.
 *
 * @param msg: key event.
 */
static void clickKey(const kbd_msg_t &msg)
{
    if ((state.settings.vpLevel == vpBeep) && !macroOpen && !msg.repeat
        && ((msg.keys & CLICK_KEYS) != 0u))
        vp_beep(BEEP_KEY_GENERIC, SHORT_BEEP);
}

/**
 * Handle a key event before the active view: wake the display, dismiss the
 * volume HUD, replay a prompt on F1, apply the keypad lock and open or close
 * the macro menu.
 *
 * @param msg: key event.
 * @param now: current tick.
 * @return true if the active view handles the event.
 */
static bool filterKey(const kbd_msg_t &msg, long long now)
{
    /* A key press waking the display only wakes it, but MONI still opens the
     * macro menu. */
    const bool woke = exitStandby(now);
    if (woke && ((msg.keys & KEY_MONI) == 0u))
        return false;

    /* Input acts on the screen under the volume HUD. */
    if (nav.overlay() == &volumeOverlay)
        nav.dismissOverlay();

    if (((msg.keys & KEY_F1) != 0u) && (state.settings.vpLevel > vpBeep)) {
        speakF1(msg);
        return false;
    }

    /* Keypad lock: while locked, ignore keys unless the macro menu is open or
     * MONI is pressed (so it can always be opened to unlock), except the FM
     * tone burst. */
    if (uiMemory.keypadLocked && !macroOpen && ((msg.keys & KEY_MONI) == 0u)
        && !isToneBurstKey(msg)) {
        clickKey(msg);
        return false;
    }

    /* Macro menu: shown while MONI is held. A long-press latches it open (if
     * enabled); a MONI press while latched, or releasing MONI when not
     * latched, closes it. MONI repeating while held does neither. */
    const bool moni = ((msg.keys & KEY_MONI) != 0u);
    if (moni && !msg.repeat) {
        if (!macroOpen) {
            nav.openOverlay(ViewId::Macro);
            macroOpen = true;
            macroLatched = false;
        } else if (macroLatched) {
            closeMacro();
            vp_beep(BEEP_FUNCTION_LATCH_OFF, LONG_BEEP);
            return false;
        }
        if (msg.long_press && (state.settings.macroMenuLatch != 0u)) {
            macroLatched = true;
            vp_beep(BEEP_FUNCTION_LATCH_ON, LONG_BEEP);
        }
    } else if (!moni && macroOpen && !macroLatched) {
        closeMacro();
        return false; /* the release only closes the menu */
    }

#if defined(CONFIG_UI_NO_KEYBOARD)
    /* Without MONI, a long VOLDOWN press opens the macro menu latched. */
    if (!macroOpen && msg.long_press && ((msg.keys & KEY_VOLDOWN) != 0u)) {
        nav.openOverlay(ViewId::Macro);
        macroOpen = true;
        macroLatched = true;
        return false;
    }
#endif

    return true;
}

/**
 * Handle one UI tick: wake-keeping, the volume HUD, and at most one queued
 * event.
 *
 * @param now: current tick.
 * @param sync_rtx: set when the radio configuration must be re-applied.
 */
static void handleTick(long long now, bool *sync_rtx)
{
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
        nav.presentOverlay(&volumeOverlay, now, kVolumeOverlayMs);

    /* Apply managed-overlay dismiss policies every tick (unconditionally, for
     * the same empty-queue reason as above). */
    nav.tickOverlay(now);

    /* Keying up closes a latched macro menu, so PTT is not left blocked. */
    if (macroLatched && platform_getPttStatus())
        closeMacro();

#if !defined(PLATFORM_TTWRPLUS)
    updateLowBattery();
#endif

    if (evQueue_rdPos != evQueue_wrPos) {
        /* Pop one event per tick. */
        const event_t raw = evQueue[evQueue_rdPos];
        evQueue_rdPos = (uint8_t)((evQueue_rdPos + 1) % MAX_NUM_EVENTS);

        kbd_msg_t msg;
        msg.value = raw.payload;
        const bool key = (raw.type == EVENT_KBD);
#if !defined(PLATFORM_TTWRPLUS)
        if (key && lowBatteryShown) {
            lowBatteryKey(msg, now);
            return;
        }
#endif
        if (key && !filterKey(msg, now))
            return;

        /* If the view handling the event edited an rtx-affecting field it flags
         * a resync, which we forward so threads.c re-applies state.channel to
         * the radio. */
        const ViewId shown = nav.activeId();
        const Event ev = Event::decode((uint8_t)raw.type, raw.payload);
        if (nav.dispatch(ev))
            *sync_rtx = true;

        if (key) {
            /* Going back from a screen beeps like reaching a first item. */
            if (((msg.keys & KEY_ESC) != 0u) && (nav.activeId() != shown))
                vp_playMenuBeepIfNeeded(true);
            clickKey(msg);
        }
        return;
    }

    /* No event this tick: blank the backlight once the idle timer elapses
     * (the wake-keeping activity was already handled at the top). */
    if (_ui_checkStandby(now - last_event_tick))
        enterStandby();
}

/**
 * Allow transmitting only from the home screen, so a PTT press in a menu or
 * editor never keys up.
 *
 * @param sync_rtx: set when the gate changes, so the rtx thread applies it.
 */
static void updatePttGate(bool *sync_rtx)
{
    const bool disable = !atHome();
    if (disable != state.pttDisable) {
        state.pttDisable = disable;
        *sync_rtx = true;
    }
}

extern "C" void ui_updateFSM(bool *sync_rtx)
{
    handleTick(getTick(), sync_rtx);
    updatePttGate(sync_rtx);
}

extern "C" bool ui_updateGUI()
{
    if (standby)
        return false; /* backlight off — nothing to draw */

#if !defined(PLATFORM_TTWRPLUS)
    if (lowBatteryShown) {
        if (!lowBattery.dirty())
            return false;
        drawScreen([&](DrawCtx &d) { lowBattery.paintTree(d); });
        lowBattery.clearDirty();
        return true;
    }
#endif

    View *view = nav.active();
    if (view == nullptr)
        return false;

    view->syncFromState(last_state);

    Screen &screen = view->screen();
    if (!screen.dirty())
        return false;

    drawScreen([&](DrawCtx &d) { screen.paintTree(d); });
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
