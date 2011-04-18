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
#include "midifile/midifstream.h"
#include "pmhelpers.h"

MfStream *stream = NULL;
MfStream *ostream = NULL;
char *ofile = NULL;

void dump(PtTimestamp timestamp, void *ignore);

int main(int argc, char **argv)
{
    FILE *f;
    PmError perr;
    PtError pterr;
    MfFile *pf, *pfo;

    if (argc < 3) {
        fprintf(stderr, "Use: streamcopyfile <in> <out>\n");
        return 1;
    }

    PSF(perr, Mf_Initialize, ());
    PTSF(pterr, Pt_Start, (1, dump, NULL));

    /* open it for input */
    SF(f, fopen, NULL, (argv[1], "rb"));
    ofile = argv[2];

    /* and read it */
    PSF(perr, Mf_ReadMidiFile, (&pf, f));
    fclose(f);

    pfo = Mf_AllocFile();
    pfo->timeDivision = 480;

    /* now start running */
    stream = Mf_OpenStream(pf);
    ostream = Mf_OpenStream(pfo);
    Mf_StartStream(stream, Pt_Time());
    Mf_StartStream(ostream, Pt_Time());

    while (1) Pt_Sleep(1<<30);

    return 0;
}

void dump(PtTimestamp timestamp, void *ignore)
{
    MfEvent *event;
    int track;
    PmEvent ev;

    if (stream == NULL || ostream == NULL) return;

    while (Mf_StreamRead(stream, &event, &track, 1) == 1) {
        ev = event->e;

        if (event->meta) {
            if (event->meta->type == MIDI_M_TEMPO && event->meta->length == 3) {
                /* send the tempo change back */
                PtTimestamp ts;
                unsigned char *data = event->meta->data;
                uint32_t tempo = (data[0] << 16) +
                    (data[1] << 8) +
                     data[2];
                tempo /= 1000;
                Mf_StreamSetTempoTick(stream, &ts, event->absoluteTm, tempo);
            }
            Mf_FreeEvent(event);
        } else {
            Mf_StreamWriteOne(ostream, track, event);
        }
    }

    if (Mf_StreamEmpty(stream) == TRUE) {
        MfFile *of;
        FILE *ofh;
        Mf_FreeFile(Mf_CloseStream(stream));
        of = Mf_CloseStream(ostream);
        SF(ofh, fopen, NULL, (ofile, "wb"));
        Mf_WriteMidiFile(ofh, of);
        fclose(ofh);
        Mf_FreeFile(of);
        exit(0);
    }
}
