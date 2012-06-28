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

#define IN_HUMIDITY_PLUGIN TempoTapper

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"
#include "hplugin.h"
#include "midifile/midi.h"
#include "midifile/midifstream.h"
#include "miditag.h"
#include "pmhelpers.h"

#define METRO_PER_QN 24

struct TempoTapperState {
    /* metronome */
    uint16_t timeDivision;
    uint8_t metronome;
    int32_t curTick;
    PtTimestamp lastTs;
};

int usage(HS);

int init(HS)
{
    struct TempoTapperState *pstate;
    SF(pstate, calloc, NULL, (1, sizeof(struct TempoTapperState)));
    pstate->metronome = METRO_PER_QN;
    pstate->curTick = -1;
    hstate->pstate[pnum] = (void *) pstate;
    return 1;
}

int begin(HS)
{
    STATE;

    /* need an input device */
    if (hstate->idev == -1) {
        usage(hstate, pnum);
        exit(1);
    }

    /* tag to say what we're doing */
    midiTagStream(hstate->ofstream, "[tempotapper]");

    /* get vital values */
    pstate->timeDivision = hstate->ifstream->file->timeDivision;

    return 1;
}

int usage(HS)
{
    fprintf(stderr, "tracktapper usage: -p tracktapper -i <input device>\n");
    return 1;
}

void handleBeat(HS, PtTimestamp ts)
{
    STATE;
    MfEvent *event;

    if (pstate->curTick < 0) {
        /* OK, this is the very first tick. Just initialize */
        hstate->nextTick = pstate->timeDivision * pstate->metronome / METRO_PER_QN;
        pstate->curTick = 0;
        pstate->lastTs = ts;
        Mf_StreamSetTempo(hstate->ifstream, ts, 0, 0, Mf_StreamGetTempo(hstate->ifstream));
    } else {
        PtTimestamp diff;
        uint32_t tempo;

        /* got a tick */
        uint32_t lastTick = pstate->curTick;
        pstate->curTick = hstate->nextTick;
        hstate->nextTick += pstate->timeDivision * pstate->metronome / METRO_PER_QN;

        /* calculate the tempo by the diff */
        diff = ts - pstate->lastTs;
        tempo = (diff * 1000) * METRO_PER_QN / pstate->metronome;
        pstate->lastTs = ts;
        if (tempo > 0) {
            MfMeta *meta;
            Mf_StreamSetTempo(hstate->ifstream, ts, 0, pstate->curTick, tempo);

            /* produce the tempo event */
            event = Mf_NewEvent();
            event->absoluteTm = lastTick;
            event->e.message = Pm_Message(MIDI_STATUS_META, 0, 0);
            event->meta = meta = Mf_NewMeta(3);
            meta->type = MIDI_M_TEMPO;
            meta->data[0] = (tempo >> 16) & 0xFF;
            meta->data[1] = (tempo >> 8) & 0xFF;
            meta->data[2] = tempo & 0xFF;
            Mf_StreamWriteOne(hstate->ofstream, 0, event);
        }
    }
}

int tickPreMidi(HS, PtTimestamp timestamp)
{
    PmEvent ev;

    while (Pm_Read(hstate->idstream, &ev, 1) == 1) {
        /* take a nonzero controller event or a note on as a tick */
        uint8_t type = Pm_MessageType(ev.message);
        uint8_t dat2 = Pm_MessageData2(ev.message);
        if ((type == MIDI_NOTE_ON || type == MIDI_CONTROLLER) && dat2 > 0) {
            handleBeat(hstate, pnum, timestamp);
        }
    }

    return 1;
}

int handleMetaEvent(HS, PtTimestamp timestamp, uint32_t tick, int rtrack, MfEvent *event, int *writeOut)
{
    STATE;

    /* we care about time signature events, to get our metronome right */
    if (event->meta->type == MIDI_M_TIME_SIGNATURE &&
            event->meta->length == MIDI_M_TIME_SIGNATURE_LENGTH) {
        pstate->metronome = MIDI_M_TIME_SIGNATURE_METRONOME(event->meta->data);
        hstate->nextTick = pstate->curTick + pstate->timeDivision * pstate->metronome / METRO_PER_QN;
    }

    return 1;
}
