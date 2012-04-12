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

#define IN_HUMIDITY_PLUGIN NoteTapper

#include <alloca.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "helpers.h"
#include "hplugin.h"
#include "midifile/midi.h"
#include "midifile/midifstream.h"
#include "miditag.h"
#include "pmhelpers.h"

#define METRO_PER_QN 24

struct NoteTapperState {
    /* track control */
    int track;

    /* metronome */
    uint16_t timeDivision;
    int32_t lastTick;
    PtTimestamp lastTs;

    /* and velocity */
    int16_t lastVelocity;
};

#define MAX_SIMUL 1024

int usage(HS);

int init(HS)
{
    struct NoteTapperState *pstate;
    SF(pstate, calloc, NULL, (1, sizeof(struct NoteTapperState)));
    pstate->track = -1;
    hstate->pstate[pnum] = (void *) pstate;
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

int begin(HS)
{
    STATE;

    /* need an input device */
    if (hstate->idev == -1) {
        usage(hstate, pnum);
        exit(1);
    }

    midiTagStream(hstate->ofstream, "[notetapper] track=%d", pstate->track);

    return 1;
}

int usage(HS)
{
    fprintf(stderr, "notetapper usage: -p notetapper -i <input device> -t <track>\n");
    return 1;
}

static void findNextTick(HS, uint32_t atleast)
{
    STATE;
    uint32_t earliest = 0x7FFFFFFF;
    int rtrack;

    /* look for the very next note */
    for (rtrack = (pstate->track < 0) ? 0 : pstate->track;
         rtrack < ((pstate->track < 0) ? hstate->ifstream->file->trackCt : (pstate->track + 1));
         rtrack++) {
        MfTrack *mtrack = hstate->ifstream->file->tracks[rtrack];
        MfEvent *cur;
        for (cur = mtrack->head; cur; cur = cur->next) {
            if (cur->absoluteTm > earliest) break;
            if (cur->absoluteTm >= atleast &&
                Pm_MessageType(cur->e.message) == MIDI_NOTE_ON &&
                Pm_MessageData2(cur->e.message) > 0) {
                earliest = cur->absoluteTm;
                break;
            }
        }
    }
    hstate->nextTick = earliest;
}

static void handleBeat(HS, PtTimestamp ts)
{
    STATE;
    int32_t curTick = 0;

    if (hstate->nextTick < 0) {
        /* OK, this is the very first tick. Just initialize */
        findNextTick(hstate, pnum, 1);
        Mf_StreamSetTempo(hstate->ifstream, ts, 0, 0, Mf_StreamGetTempo(hstate->ifstream));

    } else {
        PtTimestamp diff;
        uint32_t tempo;

        /* got a tick */
        curTick = hstate->nextTick;
        findNextTick(hstate, pnum, curTick + 1);

        /* pstate->lastTick is the tick of the last note (still ringing)
         * curTick is the tick of the current note (from these two we calculate the tempo)
         * pstate->nextTick is the tick of the upcoming note */
        diff = ts - pstate->lastTs;
        tempo = (diff * 1000) * pstate->timeDivision / (curTick - pstate->lastTick);
        if (tempo > 0) {
            MfEvent *event;
            MfMeta *meta;
            Mf_StreamSetTempo(hstate->ifstream, ts, 0, curTick, tempo);

            /* produce the tempo event */
            event = Mf_NewEvent();
            event->absoluteTm = pstate->lastTick;
            event->e.message = Pm_Message(MIDI_STATUS_META, 0, 0);
            event->meta = meta = Mf_NewMeta(MIDI_M_TEMPO_LENGTH);
            meta->type = MIDI_M_TEMPO;
            MIDI_M_TEMPO_N_SET(meta->data, tempo);
            Mf_StreamWriteOne(hstate->ofstream, 0, event);

        } else {
            /* always need to set some tick/tempo or the timing will be off */
            Mf_StreamSetTempo(hstate->ifstream, ts, 0, curTick, Mf_StreamGetTempo(hstate->ifstream));

        }
    }

    pstate->lastTick = curTick;
    pstate->lastTs = ts;
}

/* controller info */
struct Controller {
    uint8_t seen, ranged, baseval, lastval;
};
struct Controller controllers[128];

void handleController(HS, PtTimestamp ts, uint8_t cnum, uint8_t val)
{
    STATE;
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
        pstate->lastVelocity = val;
    } else if (val > 0) {
        handleBeat(hstate, pnum, ts);
    }
}

int tickPreMidi(HS, PtTimestamp timestamp)
{
    STATE;
    PmEvent ev;

    /* wait for an event from the input device */
    while (Pm_Read(hstate->idstream, &ev, 1) == 1) {
        /* looking for a MIDI_NOTE_ON */
        uint8_t type = Pm_MessageType(ev.message);
        if (type == MIDI_NOTE_ON) {
            uint8_t velocity = Pm_MessageData2(ev.message);

            /* some keyboards (read: mine) seem to think it's funny to send
             * note on events with velocity 0 instead of note off events */
            if (velocity == 0) continue;

            /* mark its velocity */
            pstate->lastVelocity = ev.message;

            /* and handle the beat */
            handleBeat(hstate, pnum, timestamp);

        } else if (type == MIDI_CONTROLLER) {
            handleController(hstate, pnum, timestamp,
                Pm_MessageData1(ev.message), Pm_MessageData2(ev.message));

        }
    }

    return 1;
}

int handleEvent(HS, PtTimestamp timestamp, uint32_t tick, int rtrack, MfEvent *event, int *writeOut)
{
    STATE;
    PmEvent ev = event->e;
    if (Pm_MessageType(ev.message) == MIDI_NOTE_ON) {
        if (Pm_MessageData2(ev.message) != 0 &&
            (pstate->track == -1 || pstate->track == rtrack)) {
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
