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

#include <alloca.h>
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

#define METRO_PER_QN 24

/* input file stream */
MfStream *ifstream = NULL;
MfStream *tstream = NULL;

/* input and output device streams */
PortMidiStream *idstream = NULL;
PortMidiStream *odstream = NULL;

int ready = 0;

/* tempo file to write to */
char *tfile = NULL;

/* metronome */
uint16_t timeDivision = 0;
uint32_t curTick = 0, nextTick = 0;
PtTimestamp lastTs = 0;

#define MAX_SIMUL 1024

void dump(PtTimestamp timestamp, void *ignore);

int peek(MfStream *stream, MfEvent **events, int *tracks, int32_t length, uint32_t *timeNext);

int main(int argc, char **argv)
{
    FILE *f;
    PmError perr;
    PtError pterr;
    MfFile *pf, *tf;
    int argi, i, rd;
    char *arg, *nextarg, *ifile;
    MfEvent *events[MAX_SIMUL];
    int tracks[MAX_SIMUL];

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
    timeDivision = pf->timeDivision;

    /* make a tempo file with the right number of tracks */
    tf = Mf_NewFile(pf->timeDivision);
    while (tf->tracks < pf->tracks) {
        Mf_NewTrack(tf);
    }
    tstream = Mf_OpenStream(tf);

    /* now start running */
    ifstream = Mf_OpenStream(pf);
    Mf_StartStream(ifstream, Pt_Time());

    /* peek for first events */
    do {
        rd = peek(ifstream, events, tracks, MAX_SIMUL, &nextTick);
        for (i = 0; i < rd; i++) {
            if (!events[i]->meta) {
                Pm_WriteShort(odstream, 0, events[i]->e.message);
            }
            Mf_FreeEvent(events[i]);
        }
    } while (nextTick == (uint32_t) -1);

    ready = 1;

    while (1) Pt_Sleep(1<<30);

    return 0;
}

void dump(PtTimestamp timestamp, void *ignore)
{
    MfEvent *events[MAX_SIMUL];
    int tracks[MAX_SIMUL];
    MfEvent *event;
    int track, rd, i;
    PmEvent ev;
    PtTimestamp ts;

    if (!ready) return;

    ts = Pt_Time();

    while (Pm_Read(idstream, &ev, 1) == 1) {
        /* take a nonzero controller event or a note on as a note */
        uint8_t type = Pm_MessageType(ev.message);
        if (type == MIDI_NOTE_ON) {
            /* got a tick */
            uint8_t velocity = Pm_MessageData2(ev.message);
            uint32_t lastTick = curTick;
            curTick = nextTick;

            /* some keyboards (read: mine) seem to think it's funny to send
             * note on events with velocity 0 instead of note off events */
            if (velocity == 0) continue;

            /* figure out when the next one is */
            while (Mf_StreamReadUntil(ifstream, &event, &track, 1, curTick) == 1) {
                if (!event->meta) {
                    if (Pm_MessageType(event->e.message) == MIDI_NOTE_ON) {
                        MfEvent *newEvent;

                        /* change its velocity */
                        event->e.message = Pm_Message(
                            Pm_MessageStatus(event->e.message),
                            Pm_MessageData1(event->e.message),
                            velocity);

                        /* and make an identical event for the "tempo" filestream */
                        newEvent = Mf_NewEvent();
                        newEvent->absoluteTm = event->absoluteTm;
                        newEvent->e.message = event->e.message;
                        Mf_StreamWriteOne(tstream, track, newEvent);
                    }
                    Pm_WriteShort(odstream, 0, event->e.message);
                }
                Mf_FreeEvent(event);
            }
            do {
                rd = peek(ifstream, events, tracks, MAX_SIMUL, &nextTick);
                for (i = 0; i < rd; i++) {
                    if (!events[i]->meta) {
                        Pm_WriteShort(odstream, 0, events[i]->e.message);
                    }
                    Mf_FreeEvent(events[i]);
                }
            } while (!Mf_StreamEmpty(ifstream) && nextTick == (uint32_t) -1);

            /* calculate the tempo by the diff */
            if (curTick != lastTick) {
                PtTimestamp diff = ts - lastTs;
                uint32_t tempo = (diff * 1000) * timeDivision / (curTick - lastTick);
                lastTs = ts;
                if (tempo > 0) {
                    MfMeta *meta;
                    Mf_StreamSetTempo(ifstream, ts, 0, curTick, tempo);

                    /* produce the tempo event */
                    event = Mf_NewEvent();
                    event->absoluteTm = lastTick;
                    event->e.message = Pm_Message(MIDI_STATUS_META, 0, 0);
                    event->meta = meta = Mf_NewMeta(3);
                    meta->type = MIDI_M_TEMPO;
                    meta->data[0] = (tempo >> 16) & 0xFF;
                    meta->data[1] = (tempo >> 8) & 0xFF;
                    meta->data[2] = tempo & 0xFF;
                    Mf_StreamWriteOne(tstream, 0, event);
                }
            }

        } else if (type != MIDI_NOTE_OFF) {
            /* send all other non-meta input events directly, as well as recording them */
            if (Pm_MessageType(ev.message) != MIDI_META) {
                for (i = 1; i < tstream->file->trackCt; i++) {
                    event = Mf_NewEvent();
                    event->absoluteTm = curTick;
                    event->e.message = Pm_Message(
                        (type<<4)|(i-1),
                        Pm_MessageData1(ev.message),
                        Pm_MessageData2(ev.message));
                    Pm_WriteShort(odstream, 0, event->e.message);
                    Mf_StreamWriteOne(tstream, i, event);
                }
            }
        }
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

int peek(MfStream *stream, MfEvent **events, int *tracks, int32_t length, uint32_t *timeNext)
{
    int rd, i, pbi, foundNote;
    MfEvent **evPutBack;
    int *tPutBack;

    /* allocate space for notes to put back */
    pbi = 0;
    evPutBack = alloca(length * sizeof(MfEvent *));
    tPutBack = alloca(length * sizeof(int));

    /* then pull them out, pulse by pulse */
    foundNote = 0;
    for (i = 0; i < length && !foundNote && !Mf_StreamEmpty(stream);) {
        *timeNext = Mf_StreamNext(stream);
        while (i < length && Mf_StreamReadUntil(stream, events + i, tracks + i, 1, *timeNext) == 1) {
            uint8_t type = Pm_MessageType(events[i]->e.message);
            if (type == MIDI_NOTE_ON || type == MIDI_NOTE_OFF) {
                /* don't read this one */
                if (type == MIDI_NOTE_ON) foundNote = 1;
                evPutBack[pbi] = events[i];
                tPutBack[pbi] = tracks[i];
                pbi++;
            } else {
                i++;
            }
        }
    }
    rd = i;
    if (rd == length) *timeNext = (uint32_t) -1;

    /* put back the ones that should be put back */
    for (i = pbi - 1; i >= 0; i--) {
        Mf_PushEventHead(stream->file->tracks[tPutBack[i]], evPutBack[i]);
    }

    return rd;
}
