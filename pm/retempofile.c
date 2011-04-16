#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"
#include "midi.h"
#include "pmf/portmidifstream.h"

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

PmfStream *stream = NULL;
PmfStream *tstream = NULL;
PortMidiStream *istream = NULL;
PortMidiStream *ostream = NULL;

void dump(PtTimestamp timestamp, void *ignore);

int main(int argc, char **argv)
{
    FILE *f;
    PmError perr;
    PtError pterr;
    PmfFile *pf, *tf;
    int argi, i;
    char *arg, *nextarg, *file;

    PmDeviceID idev = -1, odev = -1;
    int list = 0;
    file = NULL;

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
        } else {
            file = arg;
        }
    }

    PSF(perr, Pm_Initialize, ());
    PSF(perr, Pmf_Initialize, ());
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

    /* open it for input/output */
    PSF(perr, Pm_OpenInput, (&istream, idev, NULL, 1024, NULL, NULL));
    PSF(perr, Pm_OpenOutput, (&ostream, odev, NULL, 1024, NULL, NULL, 0));

    /* open the file for input */
    SF(f, fopen, NULL, (file, "rb"));

    /* and read it */
    PSF(perr, Pmf_ReadMidiFile, (&pf, f));
    fclose(f);

    /* now start running */
    stream = Pmf_OpenStream(pf);
    Pmf_StartStream(stream, Pt_Time());

    tf = Pmf_AllocFile();
    tf->timeDivision = pf->timeDivision;
    tstream = Pmf_OpenStream(tf);
    Pmf_StartStream(stream, Pt_Time());

    while (1) Pt_Sleep(1<<30);

    return 0;
}

void dump(PtTimestamp timestamp, void *ignore)
{
    PmfEvent *event;
    int track;
    PmEvent ev;
    static int baseval = -1;
    PtTimestamp ts;
    static uint32_t correctTempo = -1;

    if (stream == NULL) return;

    while (Pm_Read(istream, &ev, 1) == 1) {
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
                Pmf_StreamSetTempoTimestamp(stream, &tick, ev.timestamp, newTempo);

                /* now box it up in an event */
                event = Pmf_AllocEvent();
                event->absoluteTm = tick;
                event->e.message = Pm_Message(0xFF, 0, 0);
                event->meta = Pmf_AllocMeta(3);
                event->meta->type = MIDI_M_TEMPO;
                event->meta->data[0] = (newTempo >> 16);
                event->meta->data[1] = (newTempo >> 8) & 0xFF;
                event->meta->data[2] = newTempo & 0xFF;
                Pmf_StreamWriteOne(tstream, 0, event);
            }
        }
    }

    while (Pmf_StreamRead(stream, &event, &track, 1) == 1) {
        ev = event->e;

        if (event->meta) {
            if (event->meta->type == MIDI_M_TEMPO && event->meta->length == 3) {
                /* send the tempo change back */
                unsigned char *data = event->meta->data;
                uint32_t tempo = (data[0] << 16) +
                    (data[1] << 8) +
                     data[2];
                correctTempo = tempo;
                Pmf_StreamSetTempoTick(stream, &ts, event->absoluteTm, tempo);
            }
        } else {
            Pm_WriteShort(ostream, 0, ev.message);
        }

        Pmf_FreeEvent(event);
    }

    if (Pmf_StreamEmpty(stream) == TRUE) {
        PmfFile *of;
        FILE *ofh;
        Pmf_FreeFile(Pmf_CloseStream(stream));
        of = Pmf_CloseStream(tstream);
        SF(ofh, fopen, NULL, ("tempo.mid", "wb"));
        Pmf_WriteMidiFile(ofh, of);
        fclose(ofh);
        Pmf_FreeFile(of);
        Pm_Terminate();
        exit(0);
    }
}
