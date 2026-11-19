/*
 * SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
 * 
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL2/SDL.h"

#include "readline/readline.h"
#include "readline/history.h"

#include "emulator.h"
#include "sdl_engine.h"
#include "core/ui.h"
#include "core/utils.h"
#include "rtx/rtx.h"

/** How long screenshot_sync waits, generous as the SDL thread may be busy. */
#define SCREENSHOT_SYNC_TIMEOUT_MS 5000

/*
 * wait_stable gives up after a timeout unless enough consecutive frames are
 * identical; a UI that redraws only on change stops sending frames once
 * settled, so a quiet spell counts as stable too.
 */
#define WAIT_STABLE_TIMEOUT_MS 5000 ///< Default timeout
#define WAIT_STABLE_FRAMES 3        ///< Default identical frames
#define WAIT_STABLE_POLL_MS 10      ///< Polling period
#define WAIT_STABLE_QUIET_MS 500    ///< Time without a frame that is stable

/**
 * UI loops after which every event queued before them has been handled: the
 * UI handles one event per loop.
 */
#define WAIT_STABLE_UI_LOOPS UI_EVENT_QUEUE_LEN

/* How often the key commands check whether the UI has read their keys. */
#define KEY_POLL_MS 1

/* Custom SDL Event to request a screenshot */
extern Uint32 SDL_Screenshot_Event;

emulator_state_t emulator_state = {
    -100.0f, // RSSI
    8.2f,    // Vbat
    3,       // mic level
    4,       // volume level
    1,       // chSelector
    false,   // PTT status
    false,   // power off
};

typedef int (*_climenu_fn)(void *self, int argc, char **argv);

typedef struct {
    char *name;
    char *description;
    void *var;
    _climenu_fn fn;
} _climenu_option;

enum shell_retvals {
    SH_ERR = -1,
    SH_CONTINUE = 0,
    SH_WHAT = 1,
    SH_EXIT_OK = 2,
};

/** Synchronous screenshots requested so far, used as request id. */
static int32_t screenshotRequests;

/** Iterations of the UI main loop, counted by emulator_getKeys(). */
static _Atomic uint32_t uiLoops;

/** Keys held down by 'keyhold', reported on every keyboard scan. */
static _Atomic keyboard_t heldKeys;

/* Key states typed by the shell, one read per keyboard scan. The shell thread
 * writes an entry and then counts it in _skq_in; the UI thread reads it and
 * then counts it in _skq_out. */
static keyboard_t _shellkeyq[25] = { 0 };
static int _skq_cap = 25;
static int _skq_head;
static int _skq_tail;
static _Atomic int _skq_in;
static _Atomic int _skq_out;

/**
 * Queue a key state for the next free keyboard scan. A 0 entry is a release,
 * which separates two presses of the same key.
 *
 * @param keys: keys pressed in that scan.
 */
static void shellkeyq_put(keyboard_t keys)
{
    if ((_skq_in - _skq_out) >= _skq_cap) {
        printf("too many keys!\n");
        return;
    }

    _shellkeyq[_skq_tail] = keys;
    _skq_in++;
    _skq_tail = (_skq_tail + 1) % _skq_cap;
}

/**
 * Wait until the UI has started some more keyboard scans. Each scan reads the
 * key state, so after two scans the state set before the call has been read
 * and the event it made is in the UI event queue.
 *
 * @param scans: number of scans to wait for.
 */
static void waitScans(uint32_t scans)
{
    const uint32_t start = uiLoops;

    while (((uiLoops - start) < scans) && !emulator_state.powerOff)
        usleep(KEY_POLL_MS * 1000);
}

/**
 * Wait until the UI has read every key state queued by the shell, and the
 * release after the last one, so that however slowly the UI runs, no press
 * is lost or merged with the next command's.
 */
static void waitKeysRead(void)
{
    while ((_skq_in > _skq_out) && !emulator_state.powerOff)
        usleep(KEY_POLL_MS * 1000);

    waitScans(2);
}

static keyboard_t keyname2keyboard(char *name)
{
    /*  The line noise at the end of this comment is a vim macro for taking the
     *  keyboard.h interface and putting it into the format further below.
     *  You can load it into vim register k with "kyy and run the macro with @k
     *  (and then you can repeat a macro register application with @@ )
     *  (substitute k with any register you like)
     *  Once you've got all the names quoted, you can J them all together into
     *  a nice block.
     *
     *   _i"ElC",
     *
     */
    char *names[] = {
        "KEY_0",     "KEY_1",       "KEY_2",     "KEY_3",      "KEY_4",
        "KEY_5",     "KEY_6",       "KEY_7",     "KEY_8",      "KEY_9",
        "KEY_STAR",  "KEY_HASH",    "KEY_ENTER", "KEY_ESC",    "KEY_UP",
        "KEY_DOWN",  "KEY_LEFT",    "KEY_RIGHT", "KEY_MONI",   "KEY_F1",
        "KEY_F2",    "KEY_F3",      "KEY_F4",    "KEY_F5",     "KEY_F6",
        "KEY_VOLUP", "KEY_VOLDOWN", "KNOB_LEFT", "KNOB_RIGHT",
    };

    int numnames = sizeof(names) / sizeof(char *);

    for (int i = 0; i < numnames; i++) {
        /*
        * +4 to skip the KEY_ on all the names, non +4 to allow for KNOB_LEFT.
        * This also means you can write KEY_LEFT as "KEY_LEFT", or "LEFT" and
        * KNOB_LEFT as "KNOB_LEFT" or "_LEFT"
        *
        * so if name == "2", this whole function will return equivalent to KEY_2 cpp define
        * and if name=="LEFT", then you get equivalent to KEY_LEFT cpp define
        * and if name=="_LEFT", then you get equivalent to KNOB_LEFT cpp define
        * and if name=="KNOB_LEFT", then you get equivalent to KNOB_LEFT cpp define
        * and if name=="KEY_2", then you get equivalent to KEY_2 cpp define.
        *
        * Of course order matters a great deal in names array, has to match the
        * bit field generated in interface/keyboard.h so double check that with
        * every update
        */

        if ((strcasecmp(name, names[i] + 4) == 0)
            || (strcasecmp(name, names[i]) == 0)) //notice case insensitive
        {
            return (1 << i);
        }
    }
    return 0;
}

/**
 * Shell command 'key': press and release keys one after the other, and wait
 * until the UI has read them all.
 *
 * @param _self: menu option.
 * @param _argc: number of key names.
 * @param _argv: key names.
 * @return SH_CONTINUE.
 */
static int pressKey(void *_self, int _argc, char **_argv)
{
    (void)_self;

    printf("Press Keys: [\n");
    keyboard_t last = 0;

    for (int i = 0; i < _argc; i++) {
        if (_argv[i] != NULL) {
            printf("\t%s, \n", _argv[i]);
            keyboard_t press = keyname2keyboard(_argv[i]);
            /* The same key twice in a row would read as one held key */
            if (press == last)
                shellkeyq_put(0);

            shellkeyq_put(press);
            last = press;
        }
    }
    printf("\t]\n");
    waitKeysRead();
    return SH_CONTINUE; // continue
}

/**
 * Shell command 'keycombo': press keys together and release them, and wait
 * until the UI has read them.
 *
 * @param _self: menu option.
 * @param _argc: number of key names.
 * @param _argv: key names.
 * @return SH_CONTINUE.
 */
static int pressMultiKeys(void *_self, int _argc, char **_argv)
{
    (void)_self;
    printf("Press Keys: [\n");
    keyboard_t combo = 0;

    for (int i = 0; i < _argc; i++) {
        if (_argv[i] != NULL) {
            printf("\t%s, \n", _argv[i]);
            combo |= keyname2keyboard(_argv[i]);
        }
    }

    shellkeyq_put(combo);
    printf("\t]\n");
    waitKeysRead();
    return SH_CONTINUE; // continue
}

/**
 * Shell command 'keyhold': hold keys down, reported on every keyboard scan,
 * until 'keyrelease'. Unlike the momentary 'key', it models a held button,
 * such as MONI for the macro menu or a key held into a long press. Returns
 * once the UI has read the press, so a 'sleep' after it times the hold.
 *
 * @param _self: menu option.
 * @param _argc: number of key names.
 * @param _argv: key names.
 * @return SH_CONTINUE.
 */
static int holdKeys(void *_self, int _argc, char **_argv)
{
    (void)_self;
    keyboard_t combo = 0;

    for (int i = 0; i < _argc; i++) {
        if (_argv[i] != NULL)
            combo |= keyname2keyboard(_argv[i]);
    }

    heldKeys = combo;
    printf("Hold keys: 0x%08X\n", (unsigned)combo);
    waitScans(2);
    return SH_CONTINUE; // continue
}

/**
 * Shell command 'keyrelease': release the keys held by 'keyhold', after the
 * UI has read them once more, so the hold lasts at least until the command.
 *
 * @param _self: menu option.
 * @param _argc: unused.
 * @param _argv: unused.
 * @return SH_CONTINUE.
 */
static int releaseKeys(void *_self, int _argc, char **_argv)
{
    (void)_self;
    (void)_argc;
    (void)_argv;

    waitScans(2);
    heldKeys = 0;
    printf("Release held keys\n");
    waitScans(2);
    return SH_CONTINUE; // continue
}

/**
 * Ask the SDL thread for a screenshot.
 *
 * @param argc: number of arguments.
 * @param argv: file name, screenshot.bmp if none.
 * @param sync: wait until the file is written.
 * @return SH_CONTINUE, or SH_ERR if the request or the save failed.
 */
static int queueScreenshot(int argc, char **argv, bool sync)
{
    char *filename = "screenshot.bmp";

    if (argc && (argv[0] != NULL)) {
        filename = argv[0];
    }

    int len = strlen(filename);

    SDL_Event e;
    SDL_zero(e);
    e.type = SDL_Screenshot_Event;
    e.user.code = sync ? ++screenshotRequests : 0; // Nonzero: report back
    e.user.data1 = malloc(len + 1);
    memset(e.user.data1, 0, len + 1);
    strcpy(e.user.data1, filename);

    if (SDL_PushEvent(&e) != 1) {
        free(e.user.data1);
        return SH_ERR;
    }

    if (!sync) {
        return SH_CONTINUE;
    }

    int err = sdlEngine_waitScreenshot(e.user.code, SCREENSHOT_SYNC_TIMEOUT_MS);
    if (err == -ETIMEDOUT) {
        printf("screenshot_sync: timed out after %d ms waiting for %s\n",
               SCREENSHOT_SYNC_TIMEOUT_MS, filename);
    } else if (err < 0) {
        printf("screenshot_sync: could not save %s\n", filename);
    }

    return (err == 0) ? SH_CONTINUE : SH_ERR;
}

/**
 * Shell command 'screenshot': save a screenshot without waiting for it.
 *
 * @param _self: menu option.
 * @param _argc: number of arguments.
 * @param _argv: file name, screenshot.bmp if none.
 * @return SH_CONTINUE, or SH_ERR if the request failed.
 */
static int screenshot(void *_self, int _argc, char **_argv)
{
    (void)_self;
    return queueScreenshot(_argc, _argv, false);
}

/**
 * Shell command 'screenshot_sync': save a screenshot and wait until the file
 * is written.
 *
 * @param _self: menu option.
 * @param _argc: number of arguments.
 * @param _argv: file name, screenshot.bmp if none.
 * @return SH_CONTINUE, or SH_ERR if the screenshot was not saved.
 */
static int screenshot_sync(void *_self, int _argc, char **_argv)
{
    (void)_self;
    return queueScreenshot(_argc, _argv, true);
}

static int setFloat(void *_self, int _argc, char **_argv)
{
    _climenu_option *self = (_climenu_option *)_self;

    if (_argc <= 0 || _argv[0] == NULL) {
        printf("%s is %f\n", self->name, *(float *)(self->var));
    } else {
        sscanf(_argv[0], "%f", (float *)self->var);
        printf("%s is %f\n", self->name, *(float *)(self->var));
    }

    return SH_CONTINUE; // continue
}

static int toggleVariable(void *_self, int _argc, char **_argv)
{
    (void)_argc;
    (void)_argv;
    _climenu_option *self = (_climenu_option *)_self;
    *(int *)self->var = !*(int *)self->var;

    return SH_CONTINUE; // continue
}

static int shell_sleep(void *_self, int _argc, char **_argv)
{
    (void)_self;

    if (!_argc || _argv[0] == NULL) {
        printf("Provide a number in milliseconds to sleep as an argument\n");
        return SH_ERR;
    }

    useconds_t sleepus = atoi(_argv[0]) * 1000;
    usleep(sleepus);
    return SH_CONTINUE;
}

/**
 * Shell command 'wait_stable': wait until the UI has handled the input given
 * so far and the screen stopped changing. Only polls during which the UI ran
 * count toward the timeout.
 *
 * @param _self: menu option.
 * @param _argc: number of arguments.
 * @param _argv: timeout in ms and number of identical frames, both optional.
 * @return SH_CONTINUE, or SH_ERR on timeout.
 */
static int shell_waitStable(void *_self, int _argc, char **_argv)
{
    (void)_self;

    uint32_t timeout = WAIT_STABLE_TIMEOUT_MS;
    uint32_t frames = WAIT_STABLE_FRAMES;

    // An argument that is missing or not a positive number keeps the default
    if ((_argc >= 1) && (_argv[0] != NULL)) {
        int value = atoi(_argv[0]);
        if (value > 0) {
            timeout = value;
        }
    }

    if ((_argc >= 2) && (_argv[1] != NULL)) {
        int value = atoi(_argv[1]);
        if (value > 0) {
            frames = value;
        }
    }

    // Only frames rendered and UI loops run from now on count
    uint32_t startLoop = uiLoops;
    uint32_t lastCount;
    uint64_t lastHash = sdlEngine_getFrameHash(&lastCount);
    uint32_t identical = 0;
    uint32_t quiet = 0; // Time since the last new frame, in ms
    uint32_t lastLoops = 0;

    for (uint32_t waited = 0; waited < timeout;) {
        usleep(WAIT_STABLE_POLL_MS * 1000);

        uint32_t count;
        uint64_t hash = sdlEngine_getFrameHash(&count);
        if (count != lastCount) {
            if ((identical > 0) && (hash == lastHash)) {
                identical++;
            } else {
                identical = 1;
            }

            lastCount = count;
            lastHash = hash;
            quiet = 0;
        } else {
            quiet += WAIT_STABLE_POLL_MS;
        }

        bool settled = (identical >= frames) || (quiet >= WAIT_STABLE_QUIET_MS);
        uint32_t loops = uiLoops - startLoop;
        if ((loops >= WAIT_STABLE_UI_LOOPS) && settled) {
            return SH_CONTINUE;
        }

        // A host too busy to run the UI does not time the wait out
        if (loops != lastLoops)
            waited += WAIT_STABLE_POLL_MS;
        lastLoops = loops;
    }

    printf("wait_stable: screen not stable after %u ms\n", timeout);
    return SH_ERR;
}

/**
 * Shell command 'wait_rtx': wait until the rtx thread is in an operating
 * state. Keying up or down takes the rtx some time, longer under load, while
 * the screen stays as it was, so wait_stable alone may settle before it.
 *
 * @param _self: menu option.
 * @param _argc: number of arguments.
 * @param _argv: OFF, RX or TX, then an optional timeout in ms.
 * @return SH_CONTINUE, or SH_ERR on an unknown state or timeout.
 */
static int shell_waitRtx(void *_self, int _argc, char **_argv)
{
    (void)_self;

    static const char *const names[] = { "OFF", "RX", "TX" };
    static const uint8_t states[] = { OFF, RX, TX };
    uint32_t timeout = WAIT_STABLE_TIMEOUT_MS;
    size_t idx = 0;

    if ((_argc < 1) || (_argv[0] == NULL))
        return SH_ERR;

    while ((idx < ARRAY_SIZE(names)) && (strcasecmp(_argv[0], names[idx]) != 0))
        idx++;
    if (idx == ARRAY_SIZE(names)) {
        printf("wait_rtx: unknown state %s\n", _argv[0]);
        return SH_ERR;
    }

    if ((_argc >= 2) && (_argv[1] != NULL) && (atoi(_argv[1]) > 0))
        timeout = atoi(_argv[1]);

    for (uint32_t waited = 0; waited < timeout; waited += WAIT_STABLE_POLL_MS) {
        if (rtx_getStatus()->opStatus == states[idx])
            return SH_CONTINUE;

        usleep(WAIT_STABLE_POLL_MS * 1000);
    }

    printf("wait_rtx: rtx not in %s after %u ms\n", names[idx], timeout);
    return SH_ERR;
}

static int shell_quit(void *_self, int _argc, char **_argv)
{
    (void)_self;
    (void)_argc;
    (void)_argv;
    printf("QUIT: 73!\n");

    //could remove history entries here, if we wanted
    return SH_EXIT_OK; //normal quit
}

static int printState(void *_self, int _argc, char **_argv)
{
    (void)_self;
    (void)_argc;
    (void)_argv;
    printf("\nCurrent state\n");
    printf("RSSI   : %f\n", emulator_state.RSSI);
    printf("Battery: %f\n", emulator_state.vbat);
    printf("Mic    : %f\n", emulator_state.micLevel);
    printf("Volume : %f\n", emulator_state.volumeLevel);
    printf("Channel: %f\n", emulator_state.chSelector);
    printf("PTT    : %s\n\n", emulator_state.PTTstatus ? "true" : "false");
    return SH_CONTINUE;
}

static int shell_nop(void *_self, int _argc, char **_argv)
{
    (void)_self;
    (void)_argc;
    (void)_argv;
    //do nothing! what it says on the tin
    return SH_CONTINUE;
}

// Forward declaration needed to include function pointer in the table below
static int shell_help(void *_self, int _argc, char **_argv);

/* clang-format off */
static _climenu_option _options[] = {
    /* name/shortcut   description            var reference, if available    method to call */
    {"rssi",    "Set rssi",     (void *) &emulator_state.RSSI,        setFloat },
    {"vbat",    "Set vbat",     (void *) &emulator_state.vbat,        setFloat },
    {"mic",     "Set miclevel", (void *) &emulator_state.micLevel,    setFloat },
    {"volume",  "Set volume",   (void *) &emulator_state.volumeLevel, setFloat },
    {"channel", "Set channel",  (void *) &emulator_state.chSelector,  setFloat },
    {"ptt",     "Toggle PTT",   (void *) &emulator_state.PTTstatus,   toggleVariable },
    {"key",     "Press keys in sequence (e.g. 'key ENTER DOWN ENTER' will descend through two menus)",
                                NULL,   pressKey
    },
    {"keycombo", "Press a bunch of keys simultaneously", NULL, pressMultiKeys },
    {"keyhold",  "Hold keys down until 'keyrelease' (e.g. 'keyhold MONI')", NULL, holdKeys },
    {"keyrelease", "Release keys held by 'keyhold'", NULL, releaseKeys },
    {"show",     "Show current radio state (ptt, rssi, etc)", NULL, printState},
    {"screenshot", "[screenshot.bmp] Save screenshot to first arg or screenshot.bmp if none given",
                                NULL,   screenshot
    },
    {"screenshot_sync", "[screenshot.bmp] Like screenshot, but return only once the file is written",
                                NULL,   screenshot_sync
    },
    {"sleep",   "Wait some number of ms",           NULL,   shell_sleep },
    {"wait_stable", "[timeout_ms=5000] [frames=3] Wait until the UI has handled all input and the screen stopped changing (that many identical frames, or none for 500 ms); only time the UI runs counts toward the timeout",
                                NULL,   shell_waitStable
    },
    {"wait_rtx", "<OFF|RX|TX> [timeout_ms=5000] Wait until the rtx is in that state, e.g. after ptt",
                                NULL,   shell_waitRtx
    },
    {"help",    "Print this help",                  NULL,   shell_help },
    {"nop",     "Do nothing (useful for comments)", NULL,   shell_nop},
    {"quit",    "Quit, close the emulator",         NULL,   shell_quit },
};
/* clang-format on */

static const int num_options = (sizeof(_options) / sizeof(_climenu_option));

static int shell_help(void *_self, int _argc, char **_argv)
{
    (void)_self;
    (void)_argc;
    (void)_argv;
    printf("OpenRTX emulator shell\n\n");

    for (int i = 0; i < num_options; i++) {
        _climenu_option *o = &_options[i];
        printf("%10s -> %s\n", o->name, o->description);
    }

    return SH_CONTINUE;
}

static _climenu_option *findMenuOption(char *tok)
{
    for (int i = 0; i < num_options; i++) {
        _climenu_option *o = &_options[i];

        /*
         * strncmp like this allows for typing shortcuts like just "r" instead
         * of the full "rssi". Priority for conflicts (like if there's "s"
         * which could mean either "show" or "screenshot" ) is set by ordering
         * in the _options array
         */
        if (strncmp(tok, o->name, strlen(tok)) == 0) {
            return o;
        }
    }

    return NULL;
}

static void striptoken(char *token)
{
    for (size_t i = 0; i < strlen(token); i++) {
        if (token[i] == '\n') {
            token[i] = 0;
        }
    }
}

static int process_line(char *line)
{
    char *token = strtok(line, " ");

    if (token == NULL) {
        return SH_ERR;
    }

    striptoken(token);
    _climenu_option *o = findMenuOption(token);
    char *args[12] = { NULL };
    int i = 0;

    for (i = 0; i < 12; i++) {
        //immediately strtok again since first is a command rest are args
        token = strtok(NULL, " ");
        if (token == NULL) {
            break;
        }
        striptoken(token);
        args[i] = token;
    }

    if (token != NULL) {
        printf("\nGot too many arguments, args truncated \n");
    }

    if (o != NULL) {
        if (o->fn != NULL) {
            return o->fn(o, i, args);
        } else {
            printf("Bad fn for o, check option array for bad data\n");
            return SH_ERR;
        }
    } else {
        return SH_WHAT; //not understood
    }
}

void *startCLIMenu(void *arg)
{
    (void)arg;

    printf("\n\n");
    char *histfile = ".emulatorsh_history";
    shell_help(NULL, 0, NULL);
    int ret = SH_CONTINUE;
    using_history();
    read_history(histfile);

    do {
        char *r = readline(">");

        if (r == NULL) {
            ret = SH_EXIT_OK;
        } else if (strlen(r) > 0) {
            add_history(r);
            ret = process_line(r);
        } else {
            ret = SH_CONTINUE;
        }

        switch (ret) {
            default:
                fflush(stdout);
                break;

            case SH_WHAT:
                printf("?\n(type h or help for help)\n");
                ret = SH_CONTINUE;
                /*printf("\n>");*/
                break;

            case SH_CONTINUE:
                /*printf("\n>");*/
                break;

            case SH_EXIT_OK:
                //normal quit
                emulator_state.powerOff = true;
                break;

            case SH_ERR:
                //error
                printf("Error running that command\n");
                ret = SH_CONTINUE;
                break;
        }

        free(r); //free the string allocated by readline
    } while ((ret == SH_CONTINUE) && (emulator_state.powerOff == false));

    fflush(stdout);
    write_history(histfile);

    return NULL;
}

void emulator_start()
{
    sdlEngine_init();

    pthread_t cli_thread;
    int err = pthread_create(&cli_thread, NULL, startCLIMenu, NULL);

    if (err) {
        printf("An error occurred starting the emulator CLI thread: %d\n", err);
    }
}

keyboard_t emulator_getKeys()
{
    // The UI thread calls this once per iteration of its main loop
    uiLoops++;

    /* Keys held by 'keyhold' are reported on every scan, with any momentary
     * key from the shell queue. */
    keyboard_t out = heldKeys;

    if (_skq_in > _skq_out) {
        out |= _shellkeyq[_skq_head];
        _shellkeyq[_skq_head] = 0;
        _skq_out++;
        _skq_head = (_skq_head + 1) % _skq_cap;
    }

    return out;
}
