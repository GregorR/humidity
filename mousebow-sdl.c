/*
 * Copyright (C) 2011, 2012  Gregor Richards
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#define IN_HUMIDITY_PLUGIN MouseBow

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <SDL/SDL.h>

#include "args.h"
#include "helpers.h"
#include "hplugin.h"
#include "midifile/midi.h"
#include "midifile/midifstream.h"
#include "pmhelpers.h"

#define SDL(into, func, bad, args) do { \
    (into) = (func) args; \
    if ((into) bad) { \
        fprintf(stderr, "%s: %s\n", #func, SDL_GetError()); \
        exit(1); \
    } \
} while (0)

#define UE_MOUSE 1

/* window size / 2 */
#define W 320
#define H 320

/* smooth over SMOOTH seconds */
#define SMOOTH 0.2

/* properties of reading the mouse */
/* to what power should we raise mouse input? 0.25 is typical */
#define MOUSE_POWER 0.25

/* what should we multiple mouse input by (after the power) for sensitivity? */
#define MOUSE_SENSITIVITY 64

/* When the mouse direction changes, how long should we wait before we play the
 * note? This isn't due to any inaccuracies or smoothing, which is done
 * directly from mouse input, but to give the user a chance to get the mouse
 * moving as fast as they would like before the note's attack. Defined in usec.
 * Note that the default is effectively no delay, as the mouse timer is 50ms
 * */
#define MOUSE_DIR_TO_NOTE_DELAY 30000

#define sign(x) (((x)<0)?-1:1)

struct MouseBowState {
    /* instantaneous velocity coming from the mouse, adjusted to be MIDI in tickPreMidi */
    int32_t velocity;

    /* velocity as-played of the last (still-playing) note as played */
    int32_t lastVelocity;

    /* velocity of the next note as written */
    int32_t nextVelocity;

    /* mouse control */
    int mouseVelocity;
    int mouseLastSign;
    struct timeval mouseLastChange;

    /* fine velocity modification through expression (controller 11) */
    int lastExpressionMod; /* last tick when we inserted an expression mod */
    int lastExpressionModVal; /* and its value */

    /* track control */
    int track;

    /* and finally, the SDL screen from which we will be reading mouse info */
    SDL_Surface *screen;
};

/* functions */
void handler(PtTimestamp timestamp, void *ignore);
static void handleBeat(HS, PtTimestamp ts);

static Uint32 mouseTimer(Uint32 ival, void *ignore);
static void fixMouse();

int init(HS)
{
    struct MouseBowState *pstate;
    SF(pstate, calloc, NULL, (1, sizeof(struct MouseBowState)));
    hstate->pstate[pnum] = (void *) pstate;
    pstate->lastVelocity = pstate->nextVelocity = -1;
    pstate->mouseVelocity = -100;
    pstate->mouseLastSign = -1;
    pstate->lastExpressionModVal = 64;
    pstate->track = -1;
    return 1;
}

int argHandler(HS, int *argi, char **argv)
{
    STATE;
    char *arg = argv[*argi];
    ARGN(t, track) {
        pstate->track = atoi(argv[++*argi]);
        ++*argi;
        return 1;
    }
    return 0;
}

int usage(HS)
{
    fprintf(stderr, "mousebow usage: -p mousebow -t <track>\n");
    return 1;
}

int begin(HS)
{
    STATE;
    int tmpi, i;

    /* if we didn't get a track, complain */
    if (pstate->track < 0) {
        usage(hstate, pnum);
        exit(1);
    }

    /* set the expression of each channel */
    for (i = 0; i < 16; i++) {
        PmMessage msg;
        msg = Pm_Message((MIDI_CONTROLLER<<4) + i, 11 /* expression */, 64);
        Pm_WriteShort(hstate->odstream, 0, msg);
    };

    /* set up SDL ... */
    SDL(tmpi, SDL_Init, < 0, (SDL_INIT_VIDEO|SDL_INIT_TIMER));
    atexit(SDL_Quit);

    /* nice default title */
    SDL_WM_SetCaption("Mouse Bow", NULL);

    /* set up our window */
    SDL(pstate->screen, SDL_SetVideoMode, == NULL, (W*2, H*2, 32, SDL_SWSURFACE));

    /* take the mouse */
    system("xset m 1");
    atexit(fixMouse);
    SDL_WarpMouse(W, H);
    SDL_AddTimer(50, mouseTimer, NULL);

    return 1;
}

int mainLoop(HS)
{
    STATE;
    SDL_Event event;
    double tdiff, vx, vy, v, vs, vsmoo = 0;
    int x, y, majorX = -1, majorY = 0, rsign = -1, signChanged = 0, chTicks = 0;
    struct timeval ta, tb;

    /* poll for events */
    gettimeofday(&ta, NULL);
    while (SDL_WaitEvent(&event)) {
        switch (event.type) {
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_q) exit(0);
                break;

            case SDL_USEREVENT:
                if (event.user.code == UE_MOUSE) {
                    int ignmouse;
                    gettimeofday(&tb, NULL);

                    /* get our new mouse state */
                    ignmouse = SDL_GetMouseState(&x, &y);
                    SDL_WarpMouse(W, H);

                    /* calculate the time */
                    tdiff = ((tb.tv_usec - ta.tv_usec) + (tb.tv_sec - ta.tv_sec) * 1000000) / 1000000.0;
                    ta = tb;

                    /* ignore it if any buttons are on */
                    if (ignmouse) break;

                    /* and velocity */
                    vx = (x - W) * tdiff;
                    vy = (y - H) * tdiff;
                    v = sqrt(pow(vx, 2) + pow(vy, 2));

                    /* if the velocity is significant, allow major changes */
                    if (fabs(v) > 0.1) {
                        /* consider whether we need to change */
                        if (fabs(vy) > fabs(vx)) {
                            if (majorX) {
                                /* switch X -> Y */
                                if (++chTicks >= 2) {
                                    majorX = 0;
                                    majorY = sign(vy);
                                    chTicks = 0;
                                }
                            } else {
                                chTicks = 0;
                                if (majorY != sign(vy)) {
                                    majorX = 0;
                                    majorY = sign(vy);
                                    rsign = 0-rsign;
                                    signChanged = 1;
                                }
                            }
                        } else {
                            if (majorY) {
                                /* switch Y -> X */
                                if (++chTicks >= 2) {
                                    majorX = sign(vx);
                                    majorY = 0;
                                    chTicks = 0;
                                }
                            } else {
                                chTicks = 0;
                                if (majorX != sign(vx)) {
                                    /* we changed directions! */
                                    majorX = sign(vx);
                                    majorY = 0;
                                    rsign = 0-rsign;
                                    signChanged = 1;
                                }
                            }
                        }

                    }

                    /* get our smoothed velocity */
                    vs = pow(v, MOUSE_POWER) * rsign;
                    if (signChanged) {
                        vsmoo = vs;
                        signChanged = 0;
                    } else {
                        if (tdiff > SMOOTH) {
                            vsmoo = vs;
                        } else {
                            vsmoo = (vsmoo * (SMOOTH - tdiff) + vs * tdiff) / SMOOTH;
                        }
                    }
                    pstate->mouseVelocity = vsmoo*MOUSE_SENSITIVITY;
                }
                break;

            case SDL_QUIT:
                exit(0);
        }
    }

    return 1;
}

int findNextTick(HS, uint32_t atleast)
{
    STATE;
    MfTrack *mtrack = hstate->ifstream->file->tracks[pstate->track];
    MfEvent *cur;
    for (cur = mtrack->head; cur; cur = cur->next) {
        if (cur->absoluteTm >= atleast &&
            Pm_MessageType(cur->e.message) == MIDI_NOTE_ON &&
            Pm_MessageData2(cur->e.message) > 0) {
            hstate->nextTick = cur->absoluteTm;
            pstate->nextVelocity = Pm_MessageData2(cur->e.message);
            return 1;
        }
    }

    /* didn't find one, set it huge */
    hstate->nextTick = 0x7FFFFFFF;
    pstate->nextVelocity = 100;
    return 0;
}

static void handleBeat(HS, PtTimestamp ts)
{
    if (hstate->nextTick < 0) {
        /* OK, this is the very first tick. Just initialize */
        findNextTick(hstate, pnum, 1);
        Mf_StreamSetTempo(hstate->ifstream, ts, 0, 0, Mf_StreamGetTempo(hstate->ifstream));

    } else {
        /* got a tick */
        int32_t curTick = hstate->nextTick;
        findNextTick(hstate, pnum, curTick + 1);

        Mf_StreamSetTempo(hstate->ifstream, ts, 0, curTick, Mf_StreamGetTempo(hstate->ifstream));
    }
}

int tickPreMidi(HS, PtTimestamp timestamp)
{
    STATE;
    pstate->velocity = abs(pstate->mouseVelocity);
    if (pstate->velocity > 127) pstate->velocity = 127;
    if (pstate->velocity != 0 && sign(pstate->mouseVelocity) != pstate->mouseLastSign) {
        int32_t usecSinceChange = 0;
        if (pstate->mouseLastChange.tv_sec == 0) {
            gettimeofday(&pstate->mouseLastChange, NULL);
        } else {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            usecSinceChange =
                (tv.tv_usec - pstate->mouseLastChange.tv_usec) +
                (tv.tv_sec - pstate->mouseLastChange.tv_sec) * 1000000;
        }
        if (usecSinceChange > MOUSE_DIR_TO_NOTE_DELAY) {
            pstate->mouseLastSign = sign(pstate->mouseVelocity);
            pstate->mouseLastChange.tv_sec = 0;
            pstate->lastVelocity = pstate->velocity;

#if 0
            /* now use the last expression to adjust the new velocity, since we can't change the expression too fast */
            pstate->lastVelocity /= (double) pstate->lastExpressionModVal / 64.0;
#endif
            pstate->lastExpressionModVal = 64;

            /* if we're too quiet, it'll barely even play, let expression take care of it */
            if (pstate->lastVelocity < 64) pstate->lastVelocity = 64;
            if (pstate->lastVelocity > 127) pstate->lastVelocity = 127;

            /* OK, let the beat go on */
            handleBeat(hstate, pnum, timestamp);
        }

    } else {
        pstate->mouseLastChange.tv_sec = 0;

    }

    return 1;
}

int tickWithMidi(HS, PtTimestamp timestamp, uint32_t tmTick)
{
    STATE;
    MfEvent *event;

    if (tmTick > pstate->lastExpressionMod) {
        int vol = ((double) pstate->velocity) / ((double) pstate->lastVelocity) * 64;
        if (vol < 0) vol = 0;
        if (vol > 127) vol = 127;
        if (abs(vol - pstate->lastExpressionModVal) > 1) {
            /* don't move so fast! */
            if (vol > pstate->lastExpressionModVal) vol = pstate->lastExpressionModVal + 1;
            else vol = pstate->lastExpressionModVal - 1;
        }
        pstate->lastExpressionModVal = vol;
        event = Mf_NewEvent();
        event->absoluteTm = tmTick;
        event->e.message = Pm_Message((MIDI_CONTROLLER<<4) + pstate->track - 1, 11 /* expression */, vol);
        Mf_StreamWriteOne(hstate->ofstream, pstate->track, event);
        Pm_WriteShort(hstate->odstream, 0, event->e.message);
        pstate->lastExpressionMod = tmTick;
    }

    return 1;
}

int handleEvent(HS, PtTimestamp timestamp, uint32_t tmTick, int rtrack, MfEvent *event, int *writeOut)
{
    STATE;
    PmEvent ev = event->e;
    if (Pm_MessageType(ev.message) == MIDI_NOTE_ON) {
        if (Pm_MessageData2(ev.message) != 0 && pstate->track == rtrack) {
            /* change the velocity */
            int32_t velocity = pstate->lastVelocity;
            if (velocity < 0) velocity = 0;
            if (velocity > 127) velocity = 127;
            ev.message = Pm_Message(
                    Pm_MessageStatus(ev.message),
                    Pm_MessageData1(ev.message),
                    velocity);

            /* and write it to our output */
            *writeOut = 1;
        }
    }
    event->e = ev;

    return 1;
}

int quit(HS, int status)
{
    SDL_Event event;

    /* FIXME: ignoring status */
    /* let SDL do the actual quit */
    memset(&event, 0, sizeof(event));
    event.type = SDL_QUIT;
    SDL_PushEvent(&event);

    return 1;
}

static Uint32 mouseTimer(Uint32 ival, void *ignore)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_USEREVENT;
    event.user.code = UE_MOUSE;
    SDL_PushEvent(&event);
    return ival;
}

static void fixMouse()
{
    system("xset m default");
}
