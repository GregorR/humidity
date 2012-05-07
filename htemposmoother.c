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

int main(int argc, char **argv)
{
    FILE *ifh, *ofh;
    PmError perr;
    MfFile *imf, *omf;
    MfStream *ims, *oms;
    int i;
    MfEvent *event, *sEvent;
    MfMeta *meta;
    int track;
    uint32_t lastTempoTick = 0;
    int32_t lastTempo = 0;

    if (argc < 3) {
        fprintf(stderr, "Use: htemposmoother <input file> <output file>\n");
        return 1;
    }

    PSF(perr, Mf_Initialize, ());

    /* open it for input */
    SF(ifh, fopen, NULL, (argv[1], "rb"));

    /* and read it in */
    PSF(perr, Mf_ReadMidiFile, (&imf, ifh));
    fclose(ifh);
    ims = Mf_OpenStream(imf);

    /* prepare for output */
    omf = Mf_NewFile(imf->timeDivision);
    oms = Mf_OpenStream(omf);

    /* now perform the actual smoothing */
    while (Mf_StreamReadUntil(ims, &event, &track, 1, (uint32_t) -1) == 1) {
        /* if it's a tempo event ... */
        if (event->meta && event->meta->type == MIDI_M_TEMPO) {
            int32_t newTempo = MIDI_M_TEMPO_N(event->meta->data);

            /* smooth out until here */
            if (lastTempo != 0) {
                for (i = 0; i < 16; i++) { /* FIXME: dynamic smoothing factor */
                    /* smooth out the tick and tempo */
                    uint32_t sTick = lastTempoTick + (event->absoluteTm - lastTempoTick) * i / 16;
                    int32_t sTempo = lastTempo + (newTempo - lastTempo) * i / 16;

                    /* make the new event */
                    sEvent = Mf_NewEvent();
                    sEvent->absoluteTm = sTick;
                    sEvent->e.message = Pm_Message(MIDI_STATUS_META, 0, 0);
                    sEvent->meta = meta = Mf_NewMeta(3);
                    meta->type = MIDI_M_TEMPO;
                    meta->data[0] = (sTempo >> 16) & 0xFF;
                    meta->data[1] = (sTempo >> 8) & 0xFF;
                    meta->data[2] = sTempo & 0xFF;
                    Mf_StreamWriteOne(oms, track, sEvent);
                }
            }

            lastTempoTick = event->absoluteTm;
            lastTempo = newTempo;

            event->deltaTm = event->e.timestamp = 0;
            Mf_StreamWriteOne(oms, 0, event);

        } else if (!event->meta) {
            /* just write it out (best not be on the tempo track! */
            sEvent = Mf_NewEvent();
            sEvent->absoluteTm = event->absoluteTm;
            sEvent->e.message = event->e.message;
            Mf_StreamWriteOne(oms, track, sEvent);

        }
    }

    /* finalize them */
    imf = Mf_CloseStream(ims);
    Mf_FreeFile(imf);
    omf = Mf_CloseStream(oms);

    /* write it out */
    SF(ofh, fopen, NULL, (argv[2], "wb"));
    PSF(perr, Mf_WriteMidiFile, (ofh, omf));
    fclose(ofh);
    Mf_FreeFile(omf);

    return 0;
}
