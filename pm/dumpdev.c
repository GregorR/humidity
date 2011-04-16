#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"
#include "midi.h"
#include "portmidi.h"
#include "porttime.h"

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

PortMidiStream *stream;

void dump(PtTimestamp ts, void *ignore);

int main(int argc, char **argv)
{
    int argi, i;
    char *arg, *nextarg;
    PmError perr;
    PtError pterr;

    PmDeviceID dev = -1;
    int list = 0;

    for (argi = 1; argi < argc; argi++) {
        arg = argv[argi];
        nextarg = argv[argi+1];
        if (arg[0] == '-') {
            if (!strcmp(arg, "-l")) {
                list = 1;
            } else if (!strcmp(arg, "-i") && nextarg) {
                dev = atoi(nextarg);
                argi++;
            } else {
                fprintf(stderr, "Invalid invocation.\n");
                exit(1);
            }
        }
    }

    PSF(perr, Pm_Initialize, ());
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
        fprintf(stderr, "Warning: Using default device.\n");
        dev = Pm_GetDefaultInputDeviceID();
    }

    /* open it for input */
    PSF(perr, Pm_OpenInput, (&stream, dev, NULL, 1024, NULL, NULL));
    PSF(perr, Pm_SetFilter, (stream, PM_FILT_ACTIVE | PM_FILT_SYSEX));

    while (1) Pt_Sleep(1<<31);

    return 0;
}

void dump(PtTimestamp ts, void *ignore)
{
    PmEvent ev;
    while (Pm_Read(stream, &ev, 1) > 0) {
        switch (Pm_MessageType(ev.message)) {
            case MIDI_CC: /* control change */
                printf("CC: ");
                break;

            default:
                printf("??" "(%X): ", Pm_MessageType(ev.message));
        }
        printf("ch%d %d %d\n", (int) Pm_MessageChannel(ev.message),
            (int) Pm_MessageData1(ev.message),
            (int) Pm_MessageData2(ev.message));
    }
}