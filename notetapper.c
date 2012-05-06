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
    /* options */
    char tempoMod, velocityMod, expressionMod;

    /* instantaneous velocity coming from a controller */
    int32_t velocity;

    /* velocity as-played of the last (still-playing) note */
    int32_t lastVelocity;

    /* fine velocity modification through expression (controller 11) */
    int lastExpressionMod; /* last tick when we inserted an expression mod */
    int lastExpressionModVal; /* and its value */

    /* track control */
    int track;

    /* metronome */
    uint16_t timeDivision;
    int32_t lastTick;
    PtTimestamp lastTs;
};

#define MAX_SIMUL 1024

int usage(HS);

int init(HS)
{
    struct NoteTapperState *pstate;
    SF(pstate, calloc, NULL, (1, sizeof(struct NoteTapperState)));
    pstate->track = -1;
    pstate->velocity = pstate->lastVelocity = 100;
    pstate->lastExpressionModVal = 64;
    hstate->pstate[pnum] = (void *) pstate;
    return 1;
}

int argHandler(HS, int *argi, char **argv)
{
    STATE;
    char *arg = argv[*argi];
    ARGN(t, track) {
        pstate->track = atoi(argv[++*argi]);
        ++*argi; return 1;

    } else ARG(r, tempo) {
        pstate->tempoMod = 1;
        ++*argi; return 1;

    } else ARG(v, velocity) {
        pstate->velocityMod = 1;
        ++*argi; return 1;

    } else ARG(e, expression) {
        pstate->velocityMod = 1;
        pstate->expressionMod = 1;
        ++*argi; return 1;

    }
    return 0;
}

int begin(HS)
{
    STATE;
    int i;

    /* need an input device */
    if (hstate->idev == -1) {
        usage(hstate, pnum);
        exit(1);
    }

    /* need some consistency to the arguments */
    if (pstate->velocityMod && !pstate->expressionMod)
        pstate->velocityMod = 1;

    if (pstate->expressionMod) {
        /* set the expression of each channel; FIXME: always writes it into the 0
         * track of the file, which will be redundant with multiple runs of
         * mousebow */
        for (i = 0; i < 16; i++) {
            MfEvent *event;
            PmMessage msg;
            event = Mf_NewEvent();
            msg = event->e.message = Pm_Message(Pm_MessageStatusGen(MIDI_CONTROLLER, i), 11 /* expression */, 64);
            Pm_WriteShort(hstate->odstream, 0, msg);
            Mf_StreamWriteOne(hstate->ofstream, 0, event);
        }
    }

    /* tag to say what we're doing */
    midiTagStream(hstate->ofstream, "[notetapper] track=%d tempo=%s velocity=%s expression=%s",
        pstate->track,
        pstate->tempoMod ? "yes" : "no",
        pstate->velocityMod ? "yes" : "no",
        pstate->expressionMod ? "yes" : "no");

    /* get vital values */
    pstate->timeDivision = hstate->ifstream->file->timeDivision;

    return 1;
}

int usage(HS)
{
    fprintf(stderr, "notetapper usage: -p notetapper -i <input device> -t <track> [options]\n"
                    "notetapper options:\n"
                    "\t-r|--tempo: Modulate tempo.\n"
                    "\t-v|--velocity: Modulate velocity.\n"
                    "\t-e|--expression: Modulate expression (implies -v).\n");
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

    /* transfer our instantaneous velocity to the beat velocity */
    pstate->lastVelocity = pstate->velocity;

    if (pstate->expressionMod) {
#if 0
        /* now use the last expression to adjust the new velocity, since we can't change the expression too fast */
        pstate->lastVelocity /= (double) pstate->lastExpressionModVal / 64.0;
#endif
        pstate->lastExpressionModVal = 64;

        /* if we're too quiet, it'll barely even play, let expression take care of it */
        if (pstate->lastVelocity < 64) pstate->lastVelocity = 64;
        if (pstate->lastVelocity > 127) pstate->lastVelocity = 127;
    }

    if (hstate->nextTick < 0) {
        /* OK, this is the very first tick. Just initialize */
        findNextTick(hstate, pnum, 1);
        Mf_StreamSetTempo(hstate->ifstream, ts, 0, 0, Mf_StreamGetTempo(hstate->ifstream));

    } else {
        PtTimestamp diff;
        uint32_t tempo = 0;

        /* got a tick */
        curTick = hstate->nextTick;
        findNextTick(hstate, pnum, curTick + 1);

        if (pstate->tempoMod) {
            /* pstate->lastTick is the tick of the last note (still ringing)
             * curTick is the tick of the current note (from these two we calculate the tempo)
             * pstate->nextTick is the tick of the upcoming note */
            diff = ts - pstate->lastTs;
            tempo = (diff * 1000) * pstate->timeDivision / (curTick - pstate->lastTick);
        }

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
        pstate->velocity = val;
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
            pstate->velocity = ev.message;

            /* and handle the beat */
            handleBeat(hstate, pnum, timestamp);

        } else if (type == MIDI_CONTROLLER) {
            handleController(hstate, pnum, timestamp,
                Pm_MessageData1(ev.message), Pm_MessageData2(ev.message));

        }
    }

    return 1;
}

int tickWithMidi(HS, PtTimestamp timestamp, uint32_t tmTick)
{
    STATE;
    int rtrack;
    MfEvent *event;

    if (pstate->expressionMod && tmTick > pstate->lastExpressionMod) {
        int vol = ((double) pstate->velocity) / ((double) pstate->lastVelocity) * 64;
        if (vol < 0) vol = 0;
        if (vol > 127) vol = 127;
#if 0
        if (abs(vol - pstate->lastExpressionModVal) > 1) {
            /* don't move so fast! */
            if (vol > pstate->lastExpressionModVal) vol = pstate->lastExpressionModVal + 1;
            else vol = pstate->lastExpressionModVal - 1;
        }
#endif
        pstate->lastExpressionModVal = vol;

        for (rtrack = (pstate->track < 0) ? 1 : pstate->track;
                rtrack < ((pstate->track < 0) ? hstate->ifstream->file->trackCt : (pstate->track + 1));
                rtrack++) {
            event = Mf_NewEvent();
            event->absoluteTm = tmTick;
            event->e.message = Pm_Message(Pm_MessageStatusGen(MIDI_CONTROLLER, rtrack - 1), 11 /* expression */, vol);
            Mf_StreamWriteOne(hstate->ofstream, rtrack, event);
            Pm_WriteShort(hstate->odstream, 0, event->e.message);
            pstate->lastExpressionMod = tmTick;
        }
    }

    return 1;
}

int handleEvent(HS, PtTimestamp timestamp, uint32_t tick, int rtrack, MfEvent *event, int *writeOut)
{
    STATE;
    PmEvent ev = event->e;

    /* we only care about changing events at all if we're modifying velocity */
    if (pstate->velocityMod) {
        if (Pm_MessageType(ev.message) == MIDI_NOTE_ON) {
            if (Pm_MessageData2(ev.message) != 0 &&
                    (pstate->track == -1 || pstate->track == rtrack)) {
                /* change the velocity */
                ev.message = Pm_Message(
                        Pm_MessageStatus(ev.message),
                        Pm_MessageData1(ev.message),
                        pstate->lastVelocity);

                /* and write it to our output */
                *writeOut = 1;
            }
        }
    }

    event->e = ev;

    return 1;
}
