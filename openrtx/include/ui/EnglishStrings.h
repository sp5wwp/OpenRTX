/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ENGLISHSTRINGS_H
#define ENGLISHSTRINGS_H

#include "ui/ui_strings.h"

const stringsTable_t englishStrings =
{
    // i18n: This should be the endonym for the language/locale; it is used in the locale selection menu
    .languageName      = N_("English"),
    .off               = N_("OFF"),
    .on                = N_("ON"),
    .banks             = N_("Banks"),
    .channels          = N_("Channels"),
    .contacts          = N_("Contacts"),
    .gps               = N_("GPS"),
    .settings          = N_("Settings"),
    .backupAndRestore  = N_("Backup & Restore"),
    .info              = N_("Info"),
    .about             = N_("About"),
    .display           = N_("Display"),
    .timeAndDate       = N_("Time & Date"),
    .fm                = N_("FM"),
    .m17               = N_("M17"),
    .dmr               = N_("DMR"),
    .defaultSettings   = N_("Default Settings"),
    .brightness        = N_("Brightness"),
    .contrast          = N_("Contrast"),
    .timer             = N_("Timer"),
    .gpsEnabled        = N_("GPS Enabled"),
    .gpsSetTime        = N_("GPS Set Time"),
    .UTCTimeZone       = N_("UTC Timezone"),
    .voice             = N_("Voice"),
    .level             = N_("Level"),
    .phonetic          = N_("Phonetic"),
    .beep              = N_("Beep"),
    .backup            = N_("Backup"),
    .restore           = N_("Restore"),
    .batteryVoltage    = N_("Bat. Voltage"),
    .batteryCharge     = N_("Bat. Charge"),
    .RSSI              = N_("RSSI"),
    .model             = N_("Model"),
    .band              = N_("Band"),
    .VHF               = N_("VHF"),
    .UHF               = N_("UHF"),
    .LCDType           = N_("LCD Type"),
    .Niccolo           = N_("Niccolo' IU2KIN"),
    .Silvano           = N_("Silvano IU2KWO"),
    .Federico          = N_("Federico IU2NUO"),
    .Fred              = N_("Fred IU2NRO"),
    .Joseph            = N_("Joseph VK7JS"),
    .allChannels       = N_("All channels"),
    .menu              = N_("Menu"),
    .gpsOff            = N_("GPS OFF"),
    .noFix             = N_("No Fix"),
    .fixLost           = N_("Fix Lost"),
    .error             = N_("ERROR"),
    .flashBackup       = N_("Flash Backup"),
    .connectToRTXTool  = N_("Connect to RTXTool"),
    .toBackupFlashAnd  = N_("to backup flash and"),
    .pressPTTToStart   = N_("press PTT to start."),
    .flashRestore      = N_("Flash Restore"),
    .toRestoreFlashAnd = N_("to restore flash and"),
    .openRTX           = N_("OpenRTX"),
    .gpsSettings       = N_("GPS Settings"),
    .m17settings       = N_("M17 Settings"),
    .callsign          = N_("Callsign"),
    .resetToDefaults   = N_("Reset to Defaults"),
    .toReset           = N_("To reset:"),
    .pressEnterTwice   = N_("Press Enter twice"),
    .macroMenu         = N_("Macro Menu"),
    .forEmergencyUse   = N_("For emergency use"),
    .pressAnyButton    = N_("press any button."),
    .accessibility     = N_("Accessibility"),
    .usedHeap          = N_("Used heap"),
    .broadcast         = N_("ALL"),
    .radioSettings     = N_("Radio Settings"),
    .offset            = N_("Offset"),
    .macroLatching     = N_("Macro Latch"),
    .noGps             = N_("No GPS"),
    .batteryIcon       = N_("Battery Icon"),
    .CTCSSTone         = N_("CTCSS Tone"),
    .CTCSSEn           = N_("CTCSS En."),
    .Encode            = N_("Encode"),
    .Decode            = N_("Decode"),
    .Both              = N_("Both"),
    .None              = N_("None"),
    .direction         = N_("Direction"),
    .step              = N_("Step"),
    .radio             = N_("Radio"),
    .CAN               = N_("CAN"),
    .canRxCheck        = N_("CAN RX Check"),
    .metaText          = N_("Meta Txt"),
    .messages          = N_("Messages"),
    .noMessages        = N_("No messages"),
    .newMessage        = N_("New Message"),
    .reply             = N_("Reply"),
    .send              = N_("Send"),
    .to                = N_("To"),
    .message           = N_("Message"),
    .squelch           = N_("Squelch"),
    // i18n: voice-operated transmit level
    .vox               = N_("Vox"),
    // i18n: how the top bar shows the battery, as an icon or a percentage
    .battery           = N_("Battery"),
    .icon              = N_("Icon"),
    .percent           = N_("Percent"),
    .theme             = N_("Theme"),
    // i18n: the default colour theme
    .standard          = N_("Standard"),
    .highContrast      = N_("High Contrast"),
    // i18n: how text is drawn, smooth or crisp
    .textStyle         = N_("Text"),
    // i18n: anti-aliased text
    .smooth            = N_("Smooth"),
    // i18n: text drawn with the pixel font, without anti-aliasing
    .crisp             = N_("Crisp"),
    // i18n: hardware version, keep it short
    .hwVersion         = N_("HW Ver"),
    .noChannels        = N_("No channels"),
    .noContacts        = N_("No contacts"),
    .resetSettings     = N_("Reset settings?"),
    .areYouSure        = N_("Are you sure?"),
    // i18n: key hint, keep the key names ENTER and ESC
    .hintOkCancel      = N_("ENTER=OK  ESC=cancel"),
    // i18n: key hint, keep the key names ENTER and ESC
    .hintConfirmReset  = N_("ENTER=RESET  ESC=back"),
    // i18n: key hint, keep the key name ENTER
    .hintSet           = N_("ENTER=set"),
    // i18n: spoken only, when the character picker opens
    .pickCharacter     = N_("Pick a character"),
    // i18n: keypad hint, keep the key names; one line on a 160 px screen
    .hintKeypadClear   = N_("2-9 *:del 0:sp   #:clear"),
    // i18n: keypad hint, keep the key names; one line on a 128 px screen
    .hintKeypadClearShort = N_("2-9 *:del  #:clr"),
    // i18n: keypad hint, keep the key names; one line on a 160 px screen
    .hintKeypadSpace   = N_("2-9:type  *:del  #:space"),
    // i18n: keypad hint, keep the key names; one line on a 128 px screen
    .hintKeypadSpaceShort = N_("2-9 *:del #sp"),
    // i18n: key hint, keep the key names and the \xEF\x95\x9A backspace glyph
    .hintArrows        = N_("Up/Dn:char (\xEF\x95\x9A del)  L/R:move"),
    .volume            = N_("Volume"),
    // i18n: radio mode, such as FM or M17
    .mode              = N_("Mode"),
    // i18n: the typed frequency is outside the radio's bands
    .outOfBand         = N_("OUT OF BAND"),
    // i18n: asks for the receive frequency to be typed
    .enterRx           = N_("ENTER RX"),
    // i18n: asks for the transmit frequency to be typed
    .enterTx           = N_("ENTER TX"),
    // i18n: key hint, keep the key names; one line on a 128 px screen
    .hintArrowsShort   = N_("Up/Dn L/R"),
    // i18n: key hint for a radio with only a knob, ENTER and ESC; keep the
    // key name ENTER; one line on a 128 px screen
    .hintKnob          = N_("Knob:char ENTER:next hold:OK"),
    // i18n: time and date entry fields, in this fixed order
    .hintDateTime      = N_("dd/mm/yy hh:mm"),
    .timer5s           = N_("5 s"),
    .timer10s          = N_("10 s"),
    .timer15s          = N_("15 s"),
    .timer20s          = N_("20 s"),
    .timer25s          = N_("25 s"),
    .timer30s          = N_("30 s"),
    .timer1min         = N_("1 min"),
    .timer2min         = N_("2 min"),
    .timer3min         = N_("3 min"),
    .timer4min         = N_("4 min"),
    .timer5min         = N_("5 min"),
    .timer15min        = N_("15 min"),
    .timer30min        = N_("30 min"),
    .timer45min        = N_("45 min"),
    .timer1hour        = N_("1 hour"),
    // i18n: GPS screen, the Maidenhead locator, keep it short
    .locator           = N_("QTH"),
    // i18n: GPS screen, altitude, keep it short
    .altitudeShort     = N_("ALT"),
    // i18n: GPS screen, speed, keep it short
    .speedShort        = N_("SPD"),
    // i18n: unit symbol of the altitude
    .metres            = N_("m"),
    // i18n: unit symbol of the speed
    .kmPerHour         = N_("km/h"),
    // i18n: spoken for the delete slot of a text field
    .deleteChar        = N_("Delete"),
    // i18n: home screen, marks the free-tuning mode as opposed to a memory
    // channel number; keep it short
    .VFO               = N_("VFO"),
    // i18n: receive, on the signal meter and the frequency entry; keep it
    // short
    .RX                = N_("RX"),
    // i18n: transmit, on the signal meter and the frequency entry; keep it
    // short
    .TX                = N_("TX"),
    // i18n: home screen, tags the sub-audio tone of the channel; keep it short
    .CTCSS             = N_("CTCSS"),
    // i18n: spoken when the time and date entry reaches the time; the time
    // fields of the time and date entry hint, in the same letters
    .timeFields        = N_("hh:mm"),
};
#endif  // ENGLISHSTRINGS_H
