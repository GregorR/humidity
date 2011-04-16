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
PortMidiStream *ostream = NULL;

void dump(PtTimestamp timestamp, void *ignore);

int main(int argc, char **argv)
{
    FILE *f;
    PmError perr;
    PtError pterr;
    PmfFile *pf;
    int argi, i;
    char *arg, *nextarg, *file;

    PmDeviceID dev = -1;
    int list = 0;
    file = NULL;

    for (argi = 1; argi < argc; argi++) {
        arg = argv[argi];
        nextarg = argv[argi+1];
        if (arg[0] == '-') {
            if (!strcmp(arg, "-l")) {
                list = 1;
            } else if (!strcmp(arg, "-o") && nextarg) {
                dev = atoi(nextarg);
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
    if (dev == -1) {
        fprintf(stderr, "No device selected.\n");
        exit(1);
    }

    /* open it for input */
    PSF(perr, Pm_OpenOutput, (&ostream, dev, NULL, 1024, NULL, NULL, 0));

    /* open it for input */
    SF(f, fopen, NULL, (file, "rb"));

    /* and read it */
    PSF(perr, Pmf_ReadMidiFile, (&pf, f));
    fclose(f);

    /* now start running */
    stream = Pmf_OpenStream(pf);
    Pmf_StartStream(stream, Pt_Time());

    while (1) Pt_Sleep(1<<30);

    return 0;
}

void dump(PtTimestamp timestamp, void *ignore)
{
    PmfEvent *event;
    PmEvent ev;
    uint8_t type;

    if (stream == NULL) return;

    while (Pmf_StreamRead(stream, &event, 1) == 1) {
        ev = event->e;

        Pm_WriteShort(ostream, 0, ev.message);

        /*printf("+%d (%d) ", event->deltaTm, event->absoluteTm);*/
    
        type = Pm_MessageType(ev.message);
#if 0
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
        } else
#endif
        if (event->meta) {
            /*printf("%02X len=%d\n", (int) event->meta->type, (int) event->meta->length);*/

            if (event->meta->type == MIDI_M_TEMPO && event->meta->length == 3) {
                /* send the tempo change back */
                PtTimestamp ts;
                unsigned char *data = event->meta->data;
                uint32_t tempo = (data[0] << 16) +
                    (data[1] << 8) +
                     data[2];
                Pmf_StreamSetTempoTick(stream, &ts, event->absoluteTm, tempo);
            }
#if 0
        } else {
            printf("???\n");
#endif
        }
    }
}
