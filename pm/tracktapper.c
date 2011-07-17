/*
 * Copyright (C) 2011  Gregor Richards
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"
#include "midifile/midi.h"
#include "midifile/midifstream.h"
#include "pmhelpers.h"

#define METRO_PER_QN 24

/* input file stream */
MfStream *ifstream = NULL;
MfStream *tstream = NULL;

/* input and output device streams */
PortMidiStream *idstream = NULL;
PortMidiStream *odstream = NULL;

int ready = 0;

/* tempo file to write to */
char *tfile = NULL;

/* metronome */
uint16_t timeDivision = 0;
uint8_t metronome = METRO_PER_QN;
int32_t curTick = -1, nextTick = -1, nextVelocity = -1;
PtTimestamp lastTs = 0;

/* and velocity */
int8_t velocityMod = 0;

/* track control */
char master = 0;
int track = 0;

/* controller info */
struct Controller {
    uint8_t seen, ranged, baseval, lastval;
};
struct Controller controllers[128];

/* functions */
void usage();
void handler(PtTimestamp timestamp, void *ignore);
void handleController(PtTimestamp ts, uint8_t cnum, uint8_t val);
void handleBeat(PtTimestamp ts);

int main(int argc, char **argv)
{
    FILE *f;
    PmError perr;
    PtError pterr;
    MfFile *pf, *tf;
    int argi, i;
    char *arg, *nextarg, *ifile;

    PmDeviceID idev = -1, odev = -1;
    int list = 0;
    ifile = tfile = NULL;

    for (argi = 1; argi < argc; argi++) {
        arg = argv[argi];
        nextarg = argv[argi+1];
        if (arg[0] == '-') {
            if (!strcmp(arg, "-l")) {
                list = 1;
            } else if (!strcmp(arg, "-i") && nextarg) {
                idev = atoi(nextarg);
                argi++;
            } else if (!strcmp(arg, "-o") && nextarg) {
                odev = atoi(nextarg);
                argi++;
            } else if (!strcmp(arg, "-m") && nextarg) {
                master = 1;
                track = atoi(nextarg);
                argi++;
            } else if (!strcmp(arg, "-s") && nextarg) {
                master = 0;
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
    memset(controllers, 0, sizeof(controllers));

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
    if (idev == -1 || odev == -1) {
        usage();
        exit(1);
    }

    /* check files */
    if (!ifile || !tfile) {
        usage();
        exit(1);
    }

    /* open it for input/output */
    PSF(perr, Pm_OpenInput, (&idstream, idev, NULL, 1024, NULL, NULL));
    PSF(perr, Pm_OpenOutput, (&odstream, odev, NULL, 1024, NULL, NULL, 0));

    /* open the file for input */
    SF(f, fopen, NULL, (ifile, "rb"));

    /* and read it */
    PSF(perr, Mf_ReadMidiFile, (&pf, f));
    fclose(f);
    timeDivision = pf->timeDivision;

    /* now start running */
    ifstream = Mf_OpenStream(pf);
    Mf_StartStream(ifstream, Pt_Time());

    tf = Mf_NewFile(pf->timeDivision);
    tstream = Mf_OpenStream(tf);

    ready = 1;

    while (1) Pt_Sleep(1<<30);

    return 0;
}

void usage()
{
    fprintf(stderr, "Usage: tracktapper -i <input device> -o <output device> {-m <track>|-s <track} <input file> <output file>\n"
                    "\ttracktapper -l: List devices\n");
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

void handleController(PtTimestamp ts, uint8_t cnum, uint8_t val)
{
    struct Controller cont = controllers[cnum];

    /* figure out what we can about it */
    if (!cont.seen) {
        cont.seen = 1;
        if (val != 0 && val != 127) {
            /* it's probably ranged */
            cont.ranged = 1;
        } else {
            cont.ranged = 0;
        }
        cont.baseval = val;
    } else if (!cont.ranged && val != 0 && val != 127) {
        /* whoops, we were wrong! */
        cont.ranged = 0;
        cont.baseval = val;
    }
    cont.lastval = val;

    controllers[cnum] = cont;

    if (cont.ranged) {
        int32_t velocity = 64 + val/2;
        velocityMod = velocity - nextVelocity;
    } else if (val > 0) {
        handleBeat(ts);
    }
}

void handleBeat(PtTimestamp ts)
{
    MfEvent *event;

    if (curTick < 0) {
        /* OK, this is the very first tick. Just initialize */
        findNextTick(1);
        curTick = 0;
        lastTs = ts;
        Mf_StreamSetTempo(ifstream, ts, 0, 0, Mf_StreamGetTempo(ifstream));
    } else {
        PtTimestamp diff;
        uint32_t tempo;

        /* got a tick */
        uint32_t lastTick = curTick;
        curTick = nextTick;
        findNextTick(curTick + 1);

        /* calculate the tempo by the diff */
        diff = ts - lastTs;
        if (curTick != lastTick) {
            tempo = (diff * 1000) * timeDivision / (curTick - lastTick);
        }
        lastTs = ts;
        if (tempo > 0) {
            MfMeta *meta;
            Mf_StreamSetTempo(ifstream, ts, 0, curTick, tempo);

            /* produce the tempo event */
            event = Mf_NewEvent();
            event->absoluteTm = lastTick;
            event->e.message = Pm_Message(MIDI_STATUS_META, 0, 0);
            event->meta = meta = Mf_NewMeta(3);
            meta->type = MIDI_M_TEMPO;
            meta->data[0] = (tempo >> 16) & 0xFF;
            meta->data[1] = (tempo >> 8) & 0xFF;
            meta->data[2] = tempo & 0xFF;
            Mf_StreamWriteOne(tstream, 0, event);
        }
    }
}

void handler(PtTimestamp timestamp, void *ignore)
{
    MfEvent *event;
    int track;
    PmEvent ev;
    PtTimestamp ts;
    uint32_t tmTick;

    if (!ready) return;

    ts = Pt_Time();

    while (Pm_Read(idstream, &ev, 1) == 1) {
        /* take a nonzero controller event or a note on as a tick */
        uint8_t type = Pm_MessageType(ev.message);
        uint8_t dat1 = Pm_MessageData1(ev.message);
        uint8_t dat2 = Pm_MessageData2(ev.message);
        if (type == MIDI_NOTE_ON && dat2 > 0) {
            handleBeat(ts);
            velocityMod = dat2 - nextVelocity;
        } else if (type == MIDI_CONTROLLER) {
            handleController(ts, dat1, dat2);
        }
    }

    /* don't do anything if we shouldn't start yet */
    if (nextTick <= 0) return;

    /* figure out when to read to */
    tmTick = Mf_StreamGetTick(ifstream, ts);
    if (tmTick >= nextTick) tmTick = nextTick - 1;

    while (Mf_StreamReadUntil(ifstream, &event, &track, 1, tmTick) == 1) {
        ev = event->e;

        if (event->meta) {
            /* we care about time signature events, to get our metronome right */
            if (event->meta->type == MIDI_M_TIME_SIGNATURE &&
                event->meta->length == MIDI_M_TIME_SIGNATURE_LENGTH) {
                metronome = MIDI_M_TIME_SIGNATURE_METRONOME(event->meta->data);
            }
        } else {
            if (Pm_MessageType(ev.message) == MIDI_NOTE_ON) {
                MfEvent *newevent;

                if (Pm_MessageData2(ev.message) != 0) {
                    /* change the velocity */
                    int32_t velocity = Pm_MessageData2(ev.message) + velocityMod;
                    if (velocity < 0) velocity = 0;
                    if (velocity > 127) velocity = 127;
                    ev.message = Pm_Message(
                        Pm_MessageStatus(ev.message),
                        Pm_MessageData1(ev.message),
                        velocity);
                }

                /* and write it to our output */
                newevent = Mf_NewEvent();
                newevent->absoluteTm = event->absoluteTm;
                newevent->e.message = ev.message;
                Mf_StreamWriteOne(tstream, track, newevent);
            }
            Pm_WriteShort(odstream, 0, ev.message);
        }

        Mf_FreeEvent(event);
    }

    if (Mf_StreamEmpty(ifstream) == TRUE) {
        MfFile *of;
        FILE *ofh;
        Mf_FreeFile(Mf_CloseStream(ifstream));
        of = Mf_CloseStream(tstream);
        SF(ofh, fopen, NULL, (tfile, "wb"));
        Mf_WriteMidiFile(ofh, of);
        fclose(ofh);
        Mf_FreeFile(of);
        Pm_Terminate();
        exit(0);
    }
}
