#include <errno.h>
#include <string.h>

#include "portmidifile.h"

/* MISCELLANY HERE */

/* default strerror */
static const char *mallocStrerror()
{
    return strerror(errno);
}

PmfAllocators Pmf_Allocators;
#define AL Pmf_Allocators

/* internal malloc-wrapper with error checking */
static void *pmfMalloc(size_t sz)
{
    void *ret = AL.malloc(sz);
    if (ret == NULL) {
        fprintf(stderr, "Error while allocating memory: %s\n", AL.strerror());
        exit(1);
    }
    return ret;
}

/* same for calloc */
static void *pmfCalloc(size_t sz)
{
    void *ret = pmfMalloc(sz);
    memset(ret, 0, sz);
    return ret;
}

/* calloc of a type */
#define pmfNew(tp) (pmfCalloc(sizeof(tp)))

/* internal functions */
static PmError Pmf_ReadMidiHeader(PmfFile **into, FILE *from);
static PmError Pmf_ReadMidiTrack(PmfFile *file, FILE *from);
static PmError Pmf_ReadMidiEvent(PmfTrack *track, FILE *from, uint32_t *sz);
static PmError Pmf_ReadMidiBignum(uint32_t *into, FILE *from, uint32_t *sz);

#define MIDI_READ_N(into, fh, n) do { \
    if (fread((into), 1, n, (fh)) != n) return pmBadData; \
} while (0)

#define MIDI_READ1(into, fh) do { \
    MIDI_READ_N(&(into), fh, 1); \
} while (0)

#define MIDI_READ2(into, fh) do { \
    unsigned char __mrbuf[2]; \
    MIDI_READ_N(__mrbuf, fh, 2); \
    (into) = (__mrbuf[0] << 8) + \
              __mrbuf[1]; \
} while (0)

#define MIDI_READ4(into, fh) do { \
    unsigned char __mrbuf[4]; \
    MIDI_READ_N(__mrbuf, fh, 4); \
    (into) = (__mrbuf[0] << 24) + \
             (__mrbuf[1] << 16) + \
             (__mrbuf[2] << 8) + \
              __mrbuf[3]; \
} while (0)

/* END MISCELLANY */

/* initialization */
PmError Pmf_Initialize()
{
    AL.malloc = malloc;
    AL.strerror = mallocStrerror;
    AL.free = free;
    return pmNoError;
}

/* MIDI file */
PmfFile *Pmf_AllocFile()
{
    return pmfNew(PmfFile);
}

void Pmf_FreeFile(PmfFile *file)
{
    int i;
    for (i = 0; i < file->trackCt; i++) {
        Pmf_FreeTrack(file->tracks[i]);
    }
    if (file->tracks) AL.free(file->tracks);
    AL.free(file);
}

/* track */
PmfTrack *Pmf_AllocTrack()
{
    return pmfNew(PmfTrack);
}

void Pmf_FreeTrack(PmfTrack *track)
{
    PmfEvent *ev = track->head, *next;
    while (ev) {
        next = ev->next;
        AL.free(ev);
        ev = next;
    }
    AL.free(track);
}

PmfTrack *Pmf_NewTrack(PmfFile *file)
{
    PmfTrack *track = Pmf_AllocTrack();
    Pmf_PushTrack(file, track);
    return track;
}

void Pmf_PushTrack(PmfFile *file, PmfTrack *track)
{
    PmfTrack **newTracks;
    if (file->tracks) {
        newTracks = pmfMalloc((file->trackCt + 1) * sizeof(PmfTrack *));
        memcpy(newTracks, file->tracks, file->trackCt * sizeof(PmfTrack *));
        newTracks[file->trackCt++] = track;
        AL.free(file->tracks);
        file->tracks = newTracks;
    } else {
        file->tracks = pmfMalloc(sizeof(PmfTrack *));
        file->tracks[0] = track;
        file->trackCt = 1;
    }
}

/* and event */
PmfEvent *Pmf_AllocEvent()
{
    return pmfNew(PmfEvent);
}

void Pmf_FreeEvent(PmfEvent *event)
{
    AL.free(event);
}

PmfEvent *Pmf_NewEvent(PmfTrack *track)
{
    PmfEvent *event = Pmf_AllocEvent();
    Pmf_PushEvent(track, event);
    return event;
}

void Pmf_PushEvent(PmfTrack *track, PmfEvent *event)
{
    if (track->tail) {
        track->tail->next = event;
        event->absoluteTm = track->tail->absoluteTm + event->deltaTm;
        track->tail = event;
    } else {
        track->head = track->tail = event;
        event->absoluteTm = event->deltaTm;
    }
}

/* read in a MIDI file */
PmError Pmf_ReadMidiFile(PmfFile **into, FILE *from)
{
    PmfFile *file;
    PmError perr;
    int i;

    if ((perr = Pmf_ReadMidiHeader(&file, from))) return perr;
    *into = file;

    for (i = 0; i < file->expectedTracks; i++) {
        if ((perr = Pmf_ReadMidiTrack(file, from))) return perr;
    }

    return pmNoError;
}

static PmError Pmf_ReadMidiHeader(PmfFile **into, FILE *from)
{
    PmfFile *file;
    char magic[4];
    uint32_t chunkSize;

    /* check that the magic is right */
    if (fread(magic, 1, 4, from) != 4) return pmBadData;
    if (memcmp(magic, "MThd", 4)) return pmBadData;

    file = Pmf_AllocFile();

    /* get the chunk size */
    MIDI_READ4(chunkSize, from);
    if (chunkSize != 6) return pmBadData;

    MIDI_READ2(file->format, from);
    MIDI_READ2(file->expectedTracks, from);
    MIDI_READ2(file->timeDivision, from);

    *into = file;
    return pmNoError;
}

static PmError Pmf_ReadMidiTrack(PmfFile *file, FILE *from)
{
    PmfTrack *track;
    PmError perr;
    char magic[4];
    uint32_t chunkSize;
    uint32_t rd;

    /* make sure it's a track */
    if (fread(magic, 1, 4, from) != 4) return pmBadData;
    if (memcmp(magic, "MTrk", 4)) return pmBadData;

    track = Pmf_NewTrack(file);

    /* get the chunk size to be read */
    MIDI_READ4(chunkSize, from);

    /* and read it */
    while (chunkSize > 0) {
        if ((perr = Pmf_ReadMidiEvent(track, from, &rd))) return perr;
        if (rd > chunkSize) return pmBadData;
        chunkSize -= rd;
    }

    return pmNoError;
}

static PmError Pmf_ReadMidiEvent(PmfTrack *track, FILE *from, uint32_t *sz)
{
    PmfEvent *event;
    PmError perr;
    uint32_t deltaTm;
    uint8_t status, data1, data2;
    uint32_t rd = 0;

    /* read the delta time */
    if ((perr = Pmf_ReadMidiBignum(&deltaTm, from, &rd))) return perr;

    event = Pmf_AllocEvent();
    event->deltaTm = deltaTm;
    Pmf_PushEvent(track, event);

    /* and the rest */
    MIDI_READ1(status, from);
    MIDI_READ1(data1, from);
    MIDI_READ1(data2, from);
    event->e.message = Pm_Message(status, data1, data2);

    return pmNoError;
}

static PmError Pmf_ReadMidiBignum(uint32_t *into, FILE *from, uint32_t *sz)
{
    uint32_t ret = 0;
    int more = 1;
    unsigned char cur;

    *sz = 0;
    while (more) {
        if (fread(&cur, 1, 1, from) != 1) return pmBadData;
        (*sz)++;

        /* is there more? */
        if (cur & 0x80) {
            more = 1;
            cur &= 0x7F;
        } else {
            more = 0;
        }

        ret <<= 8;
        ret += cur;
    }

    *into = ret;
    return pmNoError;
}

/* write out a MIDI file */
PmError Pmf_WriteMidiFile(FILE *into, PmfFile *from) {return pmBadData;}
