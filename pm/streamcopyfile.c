#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "helpers.h"
#include "midi.h"
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

MfStream *stream = NULL;
MfStream *ostream = NULL;
char *ofile = NULL;

void dump(PtTimestamp timestamp, void *ignore);

int main(int argc, char **argv)
{
    FILE *f;
    PmError perr;
    PtError pterr;
    MfFile *pf, *pfo;

    if (argc < 3) {
        fprintf(stderr, "Use: streamcopyfile <in> <out>\n");
        return 1;
    }

    PSF(perr, Mf_Initialize, ());
    PTSF(pterr, Pt_Start, (1, dump, NULL));

    /* open it for input */
    SF(f, fopen, NULL, (argv[1], "rb"));
    ofile = argv[2];

    /* and read it */
    PSF(perr, Mf_ReadMidiFile, (&pf, f));
    fclose(f);

    pfo = Mf_AllocFile();
    pfo->timeDivision = 480;

    /* now start running */
    stream = Mf_OpenStream(pf);
    ostream = Mf_OpenStream(pfo);
    Mf_StartStream(stream, Pt_Time());
    Mf_StartStream(ostream, Pt_Time());

    while (1) Pt_Sleep(1<<30);

    return 0;
}

void dump(PtTimestamp timestamp, void *ignore)
{
    MfEvent *event;
    int track;
    PmEvent ev;

    if (stream == NULL || ostream == NULL) return;

    while (Mf_StreamRead(stream, &event, &track, 1) == 1) {
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
                Mf_StreamSetTempoTick(stream, &ts, event->absoluteTm, tempo);
            }
            Mf_FreeEvent(event);
        } else {
            Mf_StreamWriteOne(ostream, track, event);
        }
    }

    if (Mf_StreamEmpty(stream) == TRUE) {
        MfFile *of;
        FILE *ofh;
        Mf_FreeFile(Mf_CloseStream(stream));
        of = Mf_CloseStream(ostream);
        SF(ofh, fopen, NULL, (ofile, "wb"));
        Mf_WriteMidiFile(ofh, of);
        fclose(ofh);
        Mf_FreeFile(of);
        exit(0);
    }
}
