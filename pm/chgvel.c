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
#include "pmhelpers.h"

int main(int argc, char **argv)
{
    FILE *f;
    PmError perr;
    MfFile *pf;
    int ti;
    MfTrack *track;
    MfEvent *cur;
    int redux;

    if (argc < 4) {
        fprintf(stderr, "Use: dumpfile <file> <output file> <range reduction>\n");
        return 1;
    }
    redux = atoi(argv[3]);

    PSF(perr, Mf_Initialize, ());

    /* open it for input */
    SF(f, fopen, NULL, (argv[1], "rb"));

    /* and read it */
    PSF(perr, Mf_ReadMidiFile, (&pf, f));
    fclose(f);

    /* redux it */
    for (ti = 0; ti < pf->trackCt; ti++) {
        track = pf->tracks[ti];
        cur = track->head;
        while (cur) {
            if (Pm_MessageType(cur->e.message) == MIDI_NOTE_ON) {
                uint8_t vel = Pm_MessageData2(cur->e.message);
                vel = 127 - (127-vel)/redux;
                cur->e.message = Pm_Message(
                    Pm_MessageStatus(cur->e.message),
                    Pm_MessageData1(cur->e.message),
                    vel);
            }
            cur = cur->next;
        }
    }

    /* write it out */
    SF(f, fopen, NULL, (argv[2], "wb"));
    PSF(perr, Mf_WriteMidiFile, (f, pf));
    fclose(f);

    return 0;
}
