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

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <SDL/SDL.h>

#include "helpers.h"
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
#define SMOOTH 0.4

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

Uint32 mouseTimer(Uint32 ival, void *ignore);

#define METRO_PER_QN 24

/* input file stream */
MfStream *ifstream = NULL;
MfStream *tstream = NULL;

/* input and output device streams */
PortMidiStream *odstream = NULL;

int ready = 0;

/* tempo file to write to */
char *tfile = NULL;

/* tick of the next note (note to wait for an event before playing) */
int32_t nextTick = -1;

/* velocity of the last (still-playing) note as played, velocity of the next note as written */
int32_t lastVelocity = -1, nextVelocity = -1;

/* mouse control */
int mouseVelocity = -100;
int mouseLastSign = -1;
struct timeval mouseLastChange;

/* fine velocity modification through volume (controller 7) */
int lastVolumeMod = 0; /* last tick when we inserted a volume mod */
int lastVolumeModVal = 64; /* and its value */

/* track control */
int track = -1;

/* functions */
void usage();
void handler(PtTimestamp timestamp, void *ignore);
void handleBeat(PtTimestamp ts);
void fixMouse();

int main(int argc, char **argv)
{
    FILE *f;
    PmError perr;
    PtError pterr;
    MfFile *pf, *tf;
    int argi, i, tmpi;
    char *arg, *nextarg, *ifile;
    SDL_Surface *screen;
    SDL_Event event;
    double tdiff, vx, vy, v, vs, vsmoo;
    int x, y, majorX = -1, majorY = 0, rsign = -1, signChanged = 0, chTicks = 0;
    struct timeval ta, tb;

    PmDeviceID odev = -1;
    int list = 0;
    ifile = tfile = NULL;

    for (argi = 1; argi < argc; argi++) {
        arg = argv[argi];
        nextarg = argv[argi+1];
        if (arg[0] == '-') {
            if (!strcmp(arg, "-l")) {
                list = 1;
            } else if (!strcmp(arg, "-o") && nextarg) {
                odev = atoi(nextarg);
                argi++;
            } else if (!strcmp(arg, "-t") && nextarg) {
                track = atoi(nextarg);
                argi++;
            } else {
                usage();
                exit(1);
            }
        } else if (!ifile) {
            ifile = arg;
        } else if (!tfile) {
            tfile = arg;
        } else {
            usage();
            exit(1);
        }
    }

    PSF(perr, Pm_Initialize, ());
    PSF(perr, Mf_Initialize, ());
    PTSF(pterr, Pt_Start, (1, handler, NULL));

    /* list devices */
    if (list) {
        int ct = Pm_CountDevices();
        PmDeviceID def = Pm_GetDefaultInputDeviceID();
        const PmDeviceInfo *devinf;

        for (i = 0; i < ct; i++) {
            devinf = Pm_GetDeviceInfo(i);
            printf("%d%s: %s%s %s\n", i, (def == i) ? "*" : "",
                (devinf->input) ? "I" : "",
                (devinf->output) ? "O" : "",
                devinf->name);
        }
    }

    /* choose device */
    if (odev == -1) {
        usage();
        exit(1);
    }

    /* check files */
    if (!ifile || !tfile || track == -1) {
        usage();
        exit(1);
    }

    /* open it for input/output */
    PSF(perr, Pm_OpenOutput, (&odstream, odev, NULL, 1024, NULL, NULL, 0));

    /* open the file for input */
    SF(f, fopen, NULL, (ifile, "rb"));

    /* and read it */
    PSF(perr, Mf_ReadMidiFile, (&pf, f));
    fclose(f);

    /* now start running */
    ifstream = Mf_OpenStream(pf);
    Mf_StartStream(ifstream, Pt_Time());

    tf = Mf_NewFile(pf->timeDivision);
    tstream = Mf_OpenStream(tf);

    /* set up SDL ... */
    SDL(tmpi, SDL_Init, < 0, (SDL_INIT_VIDEO|SDL_INIT_TIMER));
    atexit(SDL_Quit);

    /* nice default title */
    SDL_WM_SetCaption("Mouse Bow", NULL);

    /* set up our window */
    SDL(screen, SDL_SetVideoMode, == NULL, (W*2, H*2, 32, SDL_SWSURFACE));

    /* take the mouse */
    system("xset m 1");
    atexit(fixMouse);
    SDL_WarpMouse(W, H);
    SDL_AddTimer(50, mouseTimer, NULL);
    gettimeofday(&ta, NULL);

    ready = 1;

    /* poll for events */
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
                    mouseVelocity = vsmoo*MOUSE_SENSITIVITY;
                }
                break;

            case SDL_QUIT:
                exit(0);
        }
    }

    return 0;
}

Uint32 mouseTimer(Uint32 ival, void *ignore)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_USEREVENT;
    event.user.code = UE_MOUSE;
    SDL_PushEvent(&event);
    return ival;
}

void usage()
{
    fprintf(stderr, "Usage: mousebow -o <output device> -t <track> [options] <input file> <output file>\n"
                    "\tmousebow -l: List devices\n");
}

int findNextTick(uint32_t atleast)
{
    MfTrack *mtrack = ifstream->file->tracks[track];
    MfEvent *cur;
    for (cur = mtrack->head; cur; cur = cur->next) {
        if (cur->absoluteTm >= atleast && Pm_MessageType(cur->e.message) == MIDI_NOTE_ON) {
            nextTick = cur->absoluteTm;
            nextVelocity = Pm_MessageData2(cur->e.message);
            return 1;
        }
    }

    /* didn't find one, set it huge */
    nextTick = 0x7FFFFFFF;
    nextVelocity = 100;
    return 0;
}

void handleBeat(PtTimestamp ts)
{
    if (nextTick < 0) {
        /* OK, this is the very first tick. Just initialize */
        findNextTick(1);
        Mf_StreamSetTempo(ifstream, ts, 0, 0, Mf_StreamGetTempo(ifstream));

    } else {
        /* got a tick */
        int32_t curTick = nextTick;
        findNextTick(curTick + 1);

        Mf_StreamSetTempo(ifstream, ts, 0, curTick, Mf_StreamGetTempo(ifstream));
    }
}

void handler(PtTimestamp timestamp, void *ignore)
{
    MfEvent *event;
    int rtrack;
    PmEvent ev;
    PtTimestamp ts;
    uint32_t tmTick;
    int velocity;

    if (!ready) return;

    ts = Pt_Time();

    velocity = abs(mouseVelocity);
    if (velocity > 127) velocity = 127;
    if (mouseVelocity != 0 && sign(mouseVelocity) != mouseLastSign) {
        int32_t usecSinceChange = 0;
        if (mouseLastChange.tv_sec == 0) {
            gettimeofday(&mouseLastChange, NULL);
        } else {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            usecSinceChange =
                (tv.tv_usec - mouseLastChange.tv_usec) +
                (tv.tv_sec - mouseLastChange.tv_sec) * 1000000;
        }
        if (usecSinceChange > MOUSE_DIR_TO_NOTE_DELAY) {
            mouseLastSign = sign(mouseVelocity);
            mouseLastChange.tv_sec = 0;
            lastVelocity = velocity;
            if (lastVelocity < 64) lastVelocity = 64;
            lastVolumeModVal = 64; /* reset volume */
            handleBeat(ts);
        }

    } else {
        mouseLastChange.tv_sec = 0;

    }

    /* don't do anything if we shouldn't start yet */
    if (nextTick <= 0) return;

    /* figure out when to read to */
    tmTick = Mf_StreamGetTick(ifstream, ts);
    if (tmTick >= nextTick) tmTick = nextTick - 1;

    /* write out our volume mod for this tick */
    else {
        if (tmTick > lastVolumeMod) {
            int vol = ((double) velocity) / ((double) lastVelocity) * 64;
            if (vol < 0) vol = 0;
            if (vol > 127) vol = 127;
            if (abs(vol - lastVolumeModVal) > 1) {
                /* don't move so fast! */
                if (vol > lastVolumeModVal) vol = lastVolumeModVal + 1;
                else vol = lastVolumeModVal - 1;
            }
            lastVolumeModVal = vol;
            event = Mf_NewEvent();
            event->absoluteTm = tmTick;
            event->e.message = Pm_Message((MIDI_CONTROLLER<<4) + track - 1, 7 /* volume */, vol);
            Mf_StreamWriteOne(tstream, 0, event);
            Pm_WriteShort(odstream, 0, event->e.message);
            lastVolumeMod = tmTick;
        }
    }

    while (Mf_StreamReadUntil(ifstream, &event, &rtrack, 1, tmTick) == 1) {
        ev = event->e;

        if (event->meta) {
            if (event->meta->type == MIDI_M_TEMPO &&
                event->meta->length == MIDI_M_TEMPO_LENGTH) {
                PtTimestamp ts;
                unsigned char *data = event->meta->data;
                uint32_t tempo = (data[0] << 16) +
                    (data[1] << 8) +
                    data[2];
                Mf_StreamSetTempoTick(ifstream, &ts, event->absoluteTm, tempo);
            }

        } else {
            if (Pm_MessageType(ev.message) == MIDI_NOTE_ON) {
                MfEvent *newevent;

                if (Pm_MessageData2(ev.message) != 0 && track == rtrack) {
                    /* change the velocity */
                    int32_t velocity = lastVelocity;
                    if (velocity < 0) velocity = 0;
                    if (velocity > 127) velocity = 127;
                    ev.message = Pm_Message(
                        Pm_MessageStatus(ev.message),
                        Pm_MessageData1(ev.message),
                        velocity);

                    /* and write it to our output */
                    newevent = Mf_NewEvent();
                    newevent->absoluteTm = event->absoluteTm;
                    newevent->e.message = ev.message;
                    Mf_StreamWriteOne(tstream, rtrack, newevent);
                }
            }
            Pm_WriteShort(odstream, 0, ev.message);
        }

        Mf_FreeEvent(event);
    }

    if (Mf_StreamEmpty(ifstream) == TRUE) {
        MfFile *of;
        FILE *ofh;
        SDL_Event event;
        Mf_FreeFile(Mf_CloseStream(ifstream));
        of = Mf_CloseStream(tstream);
        SF(ofh, fopen, NULL, (tfile, "wb"));
        Mf_WriteMidiFile(ofh, of);
        fclose(ofh);
        Mf_FreeFile(of);
        Pm_Terminate();

        /* let SDL do the actual quit */
        memset(&event, 0, sizeof(event));
        event.type = SDL_QUIT;
        SDL_PushEvent(&event);
        ready = 0;
    }
}

void fixMouse()
{
    system("xset m default");
}
