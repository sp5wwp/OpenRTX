/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

/*
 * core/ui.h for the widget toolkit: decodes events into the active view,
 * repaints it when dirty and sends it to the display.
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

#include "base/Event.hpp"
#include "app/Navigator.hpp"
#include "views/UiMemory.hpp"
#include "render/DrawCtx.hpp"
#include "style/SemanticColor.hpp"
#include "views/LowBatteryScreen.hpp"
#include "views/VfoView.hpp"
#include "app/ViewSlot.hpp"
#include "views/VolumeOverlayView.hpp"

#include <cstdint>

using namespace ortxui;

/* Event ring: pushed and popped with state_mutex held. */
static event_t evQueue[UI_EVENT_QUEUE_LEN];
static uint8_t evQueue_rdPos = 0;
static uint8_t evQueue_wrPos = 0;

/* Snapshot of the radio state the views are drawn from, copied under
 * state_mutex by ui_saveState(). */
static state_t last_state;

/* The one live view, the screen stack over it, and the volume HUD, which
 * stays built because it draws over the screen without replacing it. */
static ViewSlot viewSlot;
static Navigator nav(viewSlot, uiMemory);
static VolumeOverlayView volumeOverlay;

/* Backlight timeout: after settings.display_timer of no activity the backlight
 * is off and nothing is drawn until a key press, RF or the volume knob. */
static bool standby = false;
static long long last_event_tick = 0;

/* Macro menu, opened by a held MONI key rather than by a view. */
static bool macroOpen = false;
static bool macroLatched = false;
static bool moniDown = false; ///< MONI was held in the last key event

#if !defined(PLATFORM_TTWRPLUS)
/* The low-battery warning, drawn instead of the active screen while shown. */
static LowBatteryScreen lowBattery;
static bool lowBatteryShown = false;
#endif

/* How long the volume HUD stays after the knob stops, in ms. */
static constexpr uint32_t VOLUME_HUD_MS = 500;

/* Backlight timeouts, in ms. */
static constexpr long long TIMER_STEP_S_MS = 5000;     ///< Per 5 s step
static constexpr long long TIMER_STEP_M_MS = 60000;    ///< Per 1 min step
static constexpr long long TIMER_STEP_15M_MS = 900000; ///< Per 15 min step
static constexpr long long TIMER_1H_MS = 3600000;      ///< One hour

/* Splash screen: the logo's centre line above the screen's middle, and the
 * callsign's baseline above the bottom. */
static constexpr int16_t SPLASH_LOGO_RISE = 6;
static constexpr int16_t SPLASH_CALL_RISE = 8;

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
 * @return true if this call left standby, so the waking key press only wakes.
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

/**
 * Check whether the backlight timeout has elapsed. C linkage for its unit
 * test.
 *
 * @param time_since_last_event: time since the last activity, in ms.
 * @return true if the display should enter standby now.
 */
extern "C" bool ui_checkStandby(long long time_since_last_event)
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
                >= (TIMER_STEP_S_MS * state.settings.display_timer);
        case TIMER_1M:
        case TIMER_2M:
        case TIMER_3M:
        case TIMER_4M:
        case TIMER_5M:
            return time_since_last_event
                >= (TIMER_STEP_M_MS
                    * (state.settings.display_timer - (TIMER_1M - 1)));
        case TIMER_15M:
        case TIMER_30M:
        case TIMER_45M:
            return time_since_last_event
                >= (TIMER_STEP_15M_MS
                    * (state.settings.display_timer - (TIMER_15M - 1)));
        case TIMER_1H:
            return time_since_last_event >= TIMER_1H_MS;
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

    /* The theme needs no applying: themeColor() reads it when drawing. */
    gfx_setFontCrisp(state.settings.crispText);

    standby = false;
    last_event_tick = getTick();
    macroOpen = false;
    macroLatched = false;
    moniDown = false;
#if !defined(PLATFORM_TTWRPLUS)
    lowBatteryShown = false;
#endif
}

extern "C" void ui_drawSplashScreen()
{
    const Point logo = { 0, static_cast<int16_t>((CONFIG_SCREEN_HEIGHT / 2)
                                                 - SPLASH_LOGO_RISE) };
    const Point call = { 0, static_cast<int16_t>(CONFIG_SCREEN_HEIGHT
                                                 - SPLASH_CALL_RISE) };
    /* The logo is drawn crisp in the accent colour, then the text setting is
     * put back for the screens after it. */
    gfx_setFontCrisp(true);
    drawScreen([&](DrawCtx &d) {
        d.text(logo, FONT_SIZE_12PT, TEXT_ALIGN_CENTER, Sem::Accent,
               "OPN\nRTX");
        d.text(call, FONT_SIZE_8PT, TEXT_ALIGN_CENTER, Sem::OnSurface,
               state.settings.callsign);
    });
    gfx_setFontCrisp(state.settings.crispText);

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
    nav.closeModal();
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
    const bool moni = ((msg.keys & KEY_MONI) != 0u);
    const bool moniPress = moni && !moniDown;
    moniDown = moni;

    /* A key press waking the display only wakes it, but MONI still opens the
     * macro menu. */
    const bool woke = exitStandby(now);
    if (woke && !moni)
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
    if (uiMemory.keypadLocked && !macroOpen && !moni && !isToneBurstKey(msg)) {
        clickKey(msg);
        return false;
    }

    /* Macro menu: a MONI press opens it and releasing MONI closes it. Held
     * long, it latches open (if enabled) until the next MONI press. Keys
     * pressed and released while MONI is held do neither. */
    if (moniPress && macroLatched) {
        closeMacro();
        vp_beep(BEEP_FUNCTION_LATCH_OFF, LONG_BEEP);
        return false;
    }
    if (moniPress && !macroOpen) {
        nav.openModal(ViewId::Macro);
        macroOpen = true;
    }
    if (moni && msg.long_press && macroOpen && !macroLatched
        && (state.settings.macroMenuLatch != 0u)) {
        macroLatched = true;
        vp_beep(BEEP_FUNCTION_LATCH_ON, LONG_BEEP);
    }
    if (!moni && macroOpen && !macroLatched) {
        closeMacro();
        return false; /* the release only closes the menu */
    }

#if defined(CONFIG_UI_NO_KEYBOARD)
    /* Without MONI, a long VOLDOWN press opens the macro menu latched. */
    if (!macroOpen && msg.long_press && ((msg.keys & KEY_VOLDOWN) != 0u)) {
        nav.openModal(ViewId::Macro);
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
    /* RF activity and the volume knob keep the display awake. Checked every
     * tick, not only on an empty queue: the volume changes with a status
     * event. */
    if ((rtx_getStatus()->opStatus == TX) || rtx_rxSquelchOpen()
        || (state.volume != last_state.volume))
        exitStandby(now);

    /* Turning the volume knob shows the volume HUD, except over the macro
     * menu. */
    if ((state.volume != last_state.volume) && !macroOpen)
        nav.presentOverlay(&volumeOverlay, now, VOLUME_HUD_MS);

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
        evQueue_rdPos =
            static_cast<uint8_t>((evQueue_rdPos + 1) % UI_EVENT_QUEUE_LEN);

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

        /* A view that changed the radio configuration asks for a resync. */
        const ViewId shown = nav.activeId();
        const Event ev = Event::decode(static_cast<uint8_t>(raw.type),
                                       raw.payload);
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

    /* No event this tick: blank the backlight once the idle timer elapses. */
    if (ui_checkStandby(now - last_event_tick))
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

extern "C" void ui_updateGUI()
{
    if (standby)
        return; /* backlight off — nothing to draw */

#if !defined(PLATFORM_TTWRPLUS)
    if (lowBatteryShown) {
        if (lowBattery.dirty()) {
            drawScreen([&](DrawCtx &d) { lowBattery.paintTree(d); });
            lowBattery.clearDirty();
        }
        return;
    }
#endif

    View *view = nav.active();
    if (view == nullptr)
        return;

    view->syncFromState(last_state);

    Screen &screen = view->screen();
    if (!screen.dirty())
        return;

    drawScreen([&](DrawCtx &d) { screen.paintTree(d); });
    screen.clearDirty();
}

extern "C" bool ui_pushEvent(const uint8_t type, const uint32_t data)
{
    const uint8_t newWrPos =
        static_cast<uint8_t>((evQueue_wrPos + 1) % UI_EVENT_QUEUE_LEN);
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
