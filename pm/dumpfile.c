#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"
#include "midi.h"
#include "pmf/portmidifile.h"

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

void dump(PmfEvent *ev);

int main(int argc, char **argv)
{
    FILE *f;
    PmError perr;
    PmfFile *pf;
    int ti;
    PmfTrack *track;
    PmfEvent *cur;

    if (argc < 2) {
        fprintf(stderr, "Use: dumpfile <file> [output file]\n");
        return 1;
    }

    PSF(perr, Pmf_Initialize, ());

    /* open it for input */
    SF(f, fopen, NULL, (argv[1], "rb"));

    /* and read it */
    PSF(perr, Pmf_ReadMidiFile, (&pf, f));
    fclose(f);

    /* maybe write it out */
    if (argc > 2) {
        SF(f, fopen, NULL, (argv[2], "wb"));
        PSF(perr, Pmf_WriteMidiFile, (f, pf));
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

void dump(PmfEvent *event)
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
