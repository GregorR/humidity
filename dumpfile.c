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

PortMidiStream *stream;

void dump(MfEvent *ev);

int main(int argc, char **argv)
{
    FILE *f;
    PmError perr;
    MfFile *pf;
    int ti;
    MfTrack *track;
    MfEvent *cur;

    if (argc < 2) {
        fprintf(stderr, "Use: dumpfile <file> [output file]\n");
        return 1;
    }

    PSF(perr, Mf_Initialize, ());

    /* open it for input */
    SF(f, fopen, NULL, (argv[1], "rb"));

    /* and read it */
    PSF(perr, Mf_ReadMidiFile, (&pf, f));
    fclose(f);

    /* maybe write it out */
    if (argc > 2) {
        SF(f, fopen, NULL, (argv[2], "wb"));
        PSF(perr, Mf_WriteMidiFile, (f, pf));
        fclose(f);
    }

    for (ti = 0; ti < pf->trackCt; ti++) {
        printf("Track %d/%d\n", ti, pf->trackCt);
        track = pf->tracks[ti];
        cur = track->head;
        while (cur) {
            dump(cur);
            cur = cur->next;
        }
    }

    return 0;
}

void dump(MfEvent *event)
{
    PmEvent ev = event->e;
    uint8_t type;

    printf("+%d (%d) ", event->deltaTm, event->absoluteTm);

    type = Pm_MessageType(ev.message);
    switch (type) {
        case MIDI_NOTE_ON:              printf("On: "); break;
        case MIDI_NOTE_OFF:             printf("Off: "); break;
        case MIDI_NOTE_AFTERTOUCH:      printf("Note aftertouch: "); break;
        case MIDI_CONTROLLER:           printf("Controller: "); break;
        case MIDI_PROGRAM_CHANGE:       printf("Program: "); break;
        case MIDI_CHANNEL_AFTERTOUCH:   printf("Channel aftertouch: "); break;
        case MIDI_PITCH_BEND:           printf("Pitch bend: "); break;
        case MIDI_META:                 printf("Meta/sysex: "); break;

        default:                        printf("??" "(%X): ", Pm_MessageType(ev.message));
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
