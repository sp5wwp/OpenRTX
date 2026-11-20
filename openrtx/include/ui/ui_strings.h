/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef UI_STRINGS_H
#define UI_STRINGS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Mark a string for translation: xgettext extracts it into the
 * template (scripts/update_pot.sh), and it compiles to the string.
 */
#define N_(s) s

/*
 * NOTE: This string table's order must not be altered as voice prompts will be
 * indexed in the same order as these strings. Append new strings at the end.
 */
typedef struct
{
    const char* languageName;
    const char* off;
    const char* on;
    const char* banks;
    const char* channels;
    const char* contacts;
    const char* gps;
    const char* settings;
    const char* backupAndRestore;
    const char* info;
    const char* about;
    const char* display;
    const char* timeAndDate;
    const char* fm;
    const char* m17;
    const char* dmr;
    const char* defaultSettings;
    const char* brightness;
    const char* contrast;
    const char* timer;
    const char* gpsEnabled;
    const char* gpsSetTime;
    const char* UTCTimeZone;
    const char* voice;
    const char* level;
    const char* phonetic;
    const char* beep;
    const char* backup;
    const char* restore;
    const char* batteryVoltage;
    const char* batteryCharge;
    const char* RSSI;
    const char* model;
    const char* band;
    const char* VHF;
    const char* UHF;
    const char* LCDType;
    const char* Niccolo;
    const char* Silvano;
    const char* Federico;
    const char* Fred;
    const char* Joseph;
    const char* allChannels;
    const char* menu;
    const char* gpsOff;
    const char* noFix;
    const char* fixLost;
    const char* error;
    const char* flashBackup;
    const char* connectToRTXTool;
    const char* toBackupFlashAnd;
    const char* pressPTTToStart;
    const char* flashRestore;
    const char* toRestoreFlashAnd;
    const char* openRTX;
    const char* gpsSettings;
    const char* m17settings;
    const char* callsign;
    const char* resetToDefaults;
    const char* toReset;
    const char* pressEnterTwice;
    const char* macroMenu;
    const char* forEmergencyUse;
    const char* pressAnyButton;
    const char* accessibility;
    const char* usedHeap;
    const char* broadcast;
    const char* radioSettings;
    const char* offset;
    const char* macroLatching;
    const char* noGps;
    const char* batteryIcon;
    const char* CTCSSTone;
    const char* CTCSSEn;
    const char* Encode;
    const char* Decode;
    const char* Both;
    const char* None;
    const char* direction;
    const char* step;
    const char* radio;
    const char* CAN;
    const char* canRxCheck;
    const char* metaText;
    const char* messages;
    const char* noMessages;
    const char* newMessage;
    const char* reply;
    const char* send;
    const char* to;
    const char* message;
    const char* squelch;
    const char* vox;
    const char* battery;
    const char* icon;
    const char* percent;
    const char* theme;
    const char* standard;
    const char* highContrast;
    const char* textStyle;
    const char* smooth;
    const char* crisp;
    const char* hwVersion;
    const char* noChannels;
    const char* noContacts;
    const char* resetSettings;
    const char* areYouSure;
    const char* hintOkCancel;
    const char* hintConfirmReset;
    const char* hintSet;
    const char* pickCharacter;
    const char* hintKeypadClear;
    const char* hintKeypadClearShort;
    const char* hintKeypadSpace;
    const char* hintKeypadSpaceShort;
    const char* hintArrows;
    const char* volume;
    const char* mode;
    const char* outOfBand;
    const char* enterRx;
    const char* enterTx;
    const char* hintArrowsShort;
    const char* hintKnob;
    const char* hintDateTime;
    const char* timer5s;
    const char* timer10s;
    const char* timer15s;
    const char* timer20s;
    const char* timer25s;
    const char* timer30s;
    const char* timer1min;
    const char* timer2min;
    const char* timer3min;
    const char* timer4min;
    const char* timer5min;
    const char* timer15min;
    const char* timer30min;
    const char* timer45min;
    const char* timer1hour;
    const char* locator;
    const char* altitudeShort;
    const char* speedShort;
    const char* metres;
    const char* kmPerHour;
    const char* deleteChar;
    const char* VFO;
    const char* RX;
    const char* TX;
    const char* CTCSS;
    const char* timeFields;
    const char* nfm;
    const char* north;
    const char* east;
    const char* south;
    const char* west;
}
stringsTable_t;

/** Built-in English table. */
extern const stringsTable_t englishStrings;

/** Table of the current language. */
extern const stringsTable_t* currentLanguage;

/**
 * Search for a given string into the string table and, if found, return its
 * offset with respect to the beginning.
 *
 * @param text: string to be searched.
 * @return text position inside the string table or -1 if the string is not
 * present.
 */
int GetEnglishStringTableOffset(const char* text);

#ifdef __cplusplus
}
#endif

#endif
