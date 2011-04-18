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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"
#include "midifile/midi.h"
#include "midifile/midifile.h"
#include "midifile/midifstream.h"
#include "pmhelpers.h"

#define MAX_SIMUL 1024

void merge(MfEvent **events, int *tracks, int sz);

int main(int argc, char **argv)
{
    FILE *ifh1, *ifh2, *ofh;
    PmError perr;
    MfFile *imf1, *imf2, *omf;
    MfStream *ims1, *ims2, *oms;
    int rd, i;
    MfEvent *events[MAX_SIMUL];
    int tracks[MAX_SIMUL];

    if (argc < 4) {
        fprintf(stderr, "Use: mergefiles <input file 1> <input file 2> <output file>\n");
        return 1;
    }

    PSF(perr, Mf_Initialize, ());

    /* open them for input */
    SF(ifh1, fopen, NULL, (argv[1], "rb"));
    SF(ifh2, fopen, NULL, (argv[2], "rb"));

    /* and read them in */
    PSF(perr, Mf_ReadMidiFile, (&imf1, ifh1));
    fclose(ifh1);
    ims1 = Mf_OpenStream(imf1);
    PSF(perr, Mf_ReadMidiFile, (&imf2, ifh2));
    fclose(ifh2);
    ims2 = Mf_OpenStream(imf2);

    /* make sure they're compatible */
    if (imf1->timeDivision != imf2->timeDivision) {
        fprintf(stderr, "The files must have the same time division.\n");
        return 1;
    }

    /* prepare for output */
    omf = Mf_NewFile(imf1->timeDivision);
    oms = Mf_OpenStream(omf);

    /* now perform the actual merge */
    while (!Mf_StreamEmpty(ims1) || !Mf_StreamEmpty(ims2)) {
        uint32_t t1, t;
        t1 = Mf_StreamNext(ims1);
        t = Mf_StreamNext(ims2);
        if (t1 < t) t = t1;

        /* read all events at this time */
        rd = Mf_StreamReadUntil(ims1, events, tracks, MAX_SIMUL, t);
        rd += Mf_StreamReadUntil(ims2, events + rd, tracks + rd, MAX_SIMUL - rd, t);

        /* merge them */
        merge(events, tracks, rd);

        /* then write them all out */
        for (i = 0; i < rd; i++) {
            /* don't write out track-end events, we'll add these ourselves */
            if (!events[i]) continue;
            if (events[i]->meta && events[i]->meta->type == 0x2F) {
                Mf_FreeEvent(events[i]);
                continue;
            }
            events[i]->deltaTm = events[i]->e.timestamp = 0;
            PSF(perr, Mf_StreamWriteOne, (oms, tracks[i], events[i]));
        }
    }

    /* finalize them */
    imf1 = Mf_CloseStream(ims1);
    Mf_FreeFile(imf1);
    imf2 = Mf_CloseStream(ims2);
    Mf_FreeFile(imf2);
    omf = Mf_CloseStream(oms);

    /* write it out */
    SF(ofh, fopen, NULL, (argv[3], "wb"));
    PSF(perr, Mf_WriteMidiFile, (ofh, omf));
    fclose(ofh);
    Mf_FreeFile(omf);

    return 0;
}

void merge(MfEvent **events, int *tracks, int sz)
{
    int i, j;
    uint8_t type1, type2, data11, data12;

    for (i = 0; i < sz; i++) {
        if (!events[i]) continue;
        type1 = Pm_MessageType(events[i]->e.message);
        data11 = Pm_MessageData1(events[i]->e.message);

        for (j = i + 1; j < sz; j++) {
            if (!events[j]) continue;
            type2 = Pm_MessageType(events[j]->e.message);
            data12 = Pm_MessageData1(events[j]->e.message);

            if (type1 == type2 && data11 == data12) {
                /* merge NOTE_ON events */
                if (type1 == MIDI_NOTE_ON) {
                    events[i]->e.message = events[j]->e.message;
                    Mf_FreeEvent(events[j]);
                    events[j] = NULL;
                }
            }
        }
    }
}
