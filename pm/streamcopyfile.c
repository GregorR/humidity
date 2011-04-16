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
PmfStream *ostream = NULL;
char *ofile = NULL;

void dump(PtTimestamp timestamp, void *ignore);

int main(int argc, char **argv)
{
    FILE *f;
    PmError perr;
    PtError pterr;
    PmfFile *pf, *pfo;

    if (argc < 3) {
        fprintf(stderr, "Use: streamcopyfile <in> <out>\n");
        return 1;
    }

    PSF(perr, Pmf_Initialize, ());
    PTSF(pterr, Pt_Start, (1, dump, NULL));

    /* open it for input */
    SF(f, fopen, NULL, (argv[1], "rb"));
    ofile = argv[2];

    /* and read it */
    PSF(perr, Pmf_ReadMidiFile, (&pf, f));
    fclose(f);

    pfo = Pmf_AllocFile();
    pfo->timeDivision = 480;

    /* now start running */
    stream = Pmf_OpenStream(pf);
    ostream = Pmf_OpenStream(pfo);
    Pmf_StartStream(stream, Pt_Time());
    Pmf_StartStream(ostream, Pt_Time());

    while (1) Pt_Sleep(1<<30);

    return 0;
}

void dump(PtTimestamp timestamp, void *ignore)
{
    PmfEvent *event;
    int track;
    PmEvent ev;

    if (stream == NULL || ostream == NULL) return;

    while (Pmf_StreamRead(stream, &event, &track, 1) == 1) {
        ev = event->e;

        if (event->meta) {
            if (event->meta->type == MIDI_M_TEMPO && event->meta->length == 3) {
                /* send the tempo change back */
                PtTimestamp ts;
                unsigned char *data = event->meta->data;
                uint32_t tempo = (data[0] << 16) +
                    (data[1] << 8) +
                     data[2];
                tempo /= 1000;
                Pmf_StreamSetTempoTick(stream, &ts, event->absoluteTm, tempo);
            }
            Pmf_FreeEvent(event);
        } else {
            Pmf_StreamWriteOne(ostream, track, event);
        }
    }

    if (Pmf_StreamEmpty(stream) == TRUE) {
        PmfFile *of;
        FILE *ofh;
        Pmf_FreeFile(Pmf_CloseStream(stream));
        of = Pmf_CloseStream(ostream);
        SF(ofh, fopen, NULL, (ofile, "wb"));
        Pmf_WriteMidiFile(ofh, of);
        fclose(ofh);
        Pmf_FreeFile(of);
        exit(0);
    }
}
