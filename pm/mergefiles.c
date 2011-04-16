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
#include "midi.h"
#include "midifile/midifile.h"
#include "midifile/midifstream.h"

#define PCHECK(perr) do { \
    if (perr != pmNoError) { \
        fprintf(stderr, "%s\n", Pm_GetErrorText(perr)); \
        exit(1); \
    } \
} while (0)

#define PSF(perr, fun, args) do { \
    perr = fun args; \
    PCHECK(perr); \
} while (0)

#define PTCHECK(perr) do { \
    if (perr != ptNoError) { \
        fprintf(stderr, "PortTime error %d\n", (int) perr); \
        exit(1); \
    } \
} while (0)

#define PTSF(perr, fun, args) do { \
    perr = fun args; \
    PTCHECK(perr); \
} while (0)

#define Pm_MessageType(msg) (Pm_MessageStatus(msg)>>4)
#define Pm_MessageChannel(msg) (Pm_MessageStatus(msg)&0xF)

void dump(MfEvent *ev);

#define MAX_SIMUL 1024

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
        fprintf(stderr, "Use: dumpfile <input file 1> <input file 2> <output file>\n");
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

        /* then write them all out */
        for (i = 0; i < rd; i++) {
            /* don't write out track-end events, we'll add these ourselves */
            if (events[i]->meta && events[i]->meta->type == 0x2F) continue;
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

void dump(MfEvent *event)
{
    PmEvent ev = event->e;
    uint8_t type;

    printf("+%d (%d) ", event->deltaTm, event->absoluteTm);

    type = Pm_MessageType(ev.message);
    switch (type) {
        case MIDI_ON: printf("On: "); break;
        case MIDI_OFF: printf("Off: "); break;
        case MIDI_NAT: printf("Note aftertouch: "); break;
        case MIDI_CC: printf("Controller: "); break;
        case MIDI_PC: printf("Program: "); break;
        case MIDI_CAT: printf("Channel aftertouch: "); break;
        case MIDI_BEND: printf("Pitch bend: "); break;
        case 0xF: printf("Meta/sysex: "); break;

        default:
            printf("??" "(%X): ", Pm_MessageType(ev.message));
    }
    if (type < 0xF) {
        printf("ch%d %d %d\n", (int) Pm_MessageChannel(ev.message),
            (int) Pm_MessageData1(ev.message),
            (int) Pm_MessageData2(ev.message));
    } else if (event->meta) {
        printf("%02X len=%d\n", (int) event->meta->type, (int) event->meta->length);
    } else {
        printf("???\n");
    }
}
