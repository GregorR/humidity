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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"
#include "midifile/midi.h"
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

MfStream *ifstream = NULL;
MfStream *tstream = NULL;
PortMidiStream *idstream = NULL;
PortMidiStream *odstream = NULL;
int ready = 0;
char *tfile = NULL;

void dump(PtTimestamp timestamp, void *ignore);

int main(int argc, char **argv)
{
    FILE *f;
    PmError perr;
    PtError pterr;
    MfFile *pf, *tf;
    int argi, i;
    char *arg, *nextarg, *ifile;

    PmDeviceID idev = -1, odev = -1;
    int list = 0;
    ifile = tfile = NULL;

    for (argi = 1; argi < argc; argi++) {
        arg = argv[argi];
        nextarg = argv[argi+1];
        if (arg[0] == '-') {
            if (!strcmp(arg, "-l")) {
                list = 1;
            } else if (!strcmp(arg, "-i") && nextarg) {
                idev = atoi(nextarg);
                argi++;
            } else if (!strcmp(arg, "-o") && nextarg) {
                odev = atoi(nextarg);
                argi++;
            } else {
                fprintf(stderr, "Invalid invocation.\n");
                exit(1);
            }
        } else if (!ifile) {
            ifile = arg;
        } else if (!tfile) {
            tfile = arg;
        } else {
            fprintf(stderr, "Invalid invocation.\n");
            exit(1);
        }
    }

    PSF(perr, Pm_Initialize, ());
    PSF(perr, Mf_Initialize, ());
    PTSF(pterr, Pt_Start, (1, dump, NULL));

    /* list devices */
    if (list) {
        int ct = Pm_CountDevices();
        PmDeviceID def = Pm_GetDefaultInputDeviceID();
        const PmDeviceInfo *devinf;

        for (i = 0; i < ct; i++) {
            devinf = Pm_GetDeviceInfo(i);
            printf("%d%s: %s%s %s\n", i, (def == i) ? "*" : "",
                (devinf->input) ? "I" : "",
                (devinf->output) ? "O" : "",
                devinf->name);
        }
    }

    /* choose device */
    if (idev == -1 || odev == -1) {
        fprintf(stderr, "No device selected.\n");
        exit(1);
    }

    /* check files */
    if (!ifile || !tfile) {
        fprintf(stderr, "Need an input file and tempo output file.\n");
        exit(1);
    }

    /* open it for input/output */
    PSF(perr, Pm_OpenInput, (&idstream, idev, NULL, 1024, NULL, NULL));
    PSF(perr, Pm_OpenOutput, (&odstream, odev, NULL, 1024, NULL, NULL, 0));

    /* open the file for input */
    SF(f, fopen, NULL, (ifile, "rb"));

    /* and read it */
    PSF(perr, Mf_ReadMidiFile, (&pf, f));
    fclose(f);

    /* now start running */
    ifstream = Mf_OpenStream(pf);
    Mf_StartStream(ifstream, Pt_Time());

    tf = Mf_NewFile(pf->timeDivision);
    tstream = Mf_OpenStream(tf);

    ready = 1;

    while (1) Pt_Sleep(1<<30);

    return 0;
}

void dump(PtTimestamp timestamp, void *ignore)
{
    MfEvent *event;
    int track;
    PmEvent ev;
    static int baseval = -1;
    PtTimestamp ts;
    static uint32_t correctTempo = -1;

    if (!ready) return;

    while (Pm_Read(idstream, &ev, 1) == 1) {
        if (Pm_MessageType(ev.message) == 0xB /* CC */ &&
            Pm_MessageData1(ev.message) == 13) {
            int cur = Pm_MessageData2(ev.message);
            if (baseval == -1) {
                baseval = cur;
            } else {
                uint32_t tick;
                uint32_t newTempo = correctTempo;
                double power;
                int adjust = (int) Pm_MessageData2(ev.message) - baseval;
                if (adjust > 0) {
                    power = (double) -adjust / (127-baseval);
                } else {
                    power = (double) -adjust / baseval;
                }
                newTempo = pow(2, power) * correctTempo;
                if (newTempo <= 0) newTempo = 1;
                printf("adj %d => %d\n", adjust, newTempo);
                Mf_StreamSetTempoTimestamp(ifstream, &tick, ev.timestamp, newTempo);

                /* now box it up in an event */
                event = Mf_NewEvent();
                event->absoluteTm = tick;
                event->e.message = Pm_Message(0xFF, 0, 0);
                event->meta = Mf_NewMeta(3);
                event->meta->type = MIDI_M_TEMPO;
                event->meta->data[0] = (newTempo >> 16);
                event->meta->data[1] = (newTempo >> 8) & 0xFF;
                event->meta->data[2] = newTempo & 0xFF;
                Mf_StreamWriteOne(tstream, 0, event);
            }
        }
    }

    while (Mf_StreamRead(ifstream, &event, &track, 1) == 1) {
        ev = event->e;

        if (event->meta) {
            if (event->meta->type == MIDI_M_TEMPO && event->meta->length == 3) {
                /* send the tempo change back */
                unsigned char *data = event->meta->data;
                uint32_t tempo = MIDI_M_TEMPO_N(data);
                correctTempo = tempo;
                Mf_StreamSetTempoTick(ifstream, &ts, event->absoluteTm, tempo);
            }
        } else {
            Pm_WriteShort(odstream, 0, ev.message);
        }

        Mf_FreeEvent(event);
    }

    if (Mf_StreamEmpty(ifstream) == TRUE) {
        MfFile *of;
        FILE *ofh;
        Mf_FreeFile(Mf_CloseStream(ifstream));
        of = Mf_CloseStream(tstream);
        SF(ofh, fopen, NULL, (tfile, "wb"));
        Mf_WriteMidiFile(ofh, of);
        fclose(ofh);
        Mf_FreeFile(of);
        Pm_Terminate();
        exit(0);
    }
}
