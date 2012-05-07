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

uint8_t metro(uint8_t numer, uint8_t denom);

int main(int argc, char **argv)
{
    FILE *f;
    PmError perr;
    MfFile *pf;
    int ti;
    MfTrack *track;
    MfEvent *cur;

    if (argc < 3) {
        fprintf(stderr, "Use: htimesigfixer <file> <output file>\n");
        return 1;
    }

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
            if (cur->meta && cur->meta->type == MIDI_M_TIME_SIGNATURE &&
                cur->meta->length == MIDI_M_TIME_SIGNATURE_LENGTH) {
                /* fix this time signature */
                uint8_t numer = MIDI_M_TIME_SIGNATURE_NUMERATOR(cur->meta->data);
                uint8_t denom = MIDI_M_TIME_SIGNATURE_DENOMINATOR(cur->meta->data);
                MIDI_M_TIME_SIGNATURE_METRONOME(cur->meta->data) =
                    metro(numer, denom);
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

#define DENOM_TICKS (96>>denom)

/* figure out a metronome value for the given time signature
 * for no good reason, this has some support for numerators up to 20 */
uint8_t metro(uint8_t numer, uint8_t denom)
{
    switch (numer) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4: /* common time and friends */
        case 5:
        case 7:
        case 11:
        case 13:
        case 17:
        case 19:
            return DENOM_TICKS;

        case 10:
            return DENOM_TICKS * 2;

        case 6:
        case 9:
        case 12:
            return DENOM_TICKS * 3;

        case 8:
        case 16:
            return DENOM_TICKS * 4;

        case 15:
        case 20:
            return DENOM_TICKS * 5;

        case 18:
            return DENOM_TICKS * 6;

        case 14:
            return DENOM_TICKS * 7;

        default:
            return metro(numer/2, (denom>0)?(denom-1):0);
    }
}
