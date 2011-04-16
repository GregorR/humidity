#include <errno.h>
#include <string.h>

#include "portmidifile.h"
#include "portmidifilealloc.h"

/* MISCELLANY HERE */

/* default strerror */
static const char *mallocStrerror()
{
    return strerror(errno);
}

/* internal functions */
static PmError Pmf_ReadMidiHeader(PmfFile **into, FILE *from);
static PmError Pmf_ReadMidiTrack(PmfFile *file, FILE *from);
static PmError Pmf_ReadMidiEvent(PmfTrack *track, FILE *from, uint8_t *pstatus, uint32_t *sz);
static PmError Pmf_ReadMidiBignum(uint32_t *into, FILE *from, uint32_t *sz);
static PmError Pmf_WriteMidiHeader(FILE *into, PmfFile *from);
static PmError Pmf_WriteMidiTrack(FILE *into, PmfTrack *track);
static PmError Pmf_WriteMidiEvent(FILE *into, PmfEvent *event, uint8_t *pstatus);
static uint32_t Pmf_GetMidiEventLength(PmfEvent *event, uint8_t *pstatus);
static PmError Pmf_WriteMidiBignum(FILE *into, uint32_t val);
static uint32_t Pmf_GetMidiBignumLength(uint32_t val);

#define BAD_DATA { *((int *) 0) = 0; return pmBadData; }

#define MIDI_READ_N(into, fh, n) do { \
    if (fread((into), 1, n, (fh)) != n) BAD_DATA; \
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

#define MIDI_WRITE1(fh, val) do { \
    fwrite(&(val), 1, 1, fh); \
} while (0)

#define MIDI_WRITE2(fh, val) do { \
    fprintf(fh, "%c%c", \
        (char) (((val) & 0xFF00) >> 8), \
        (char) ( (val) & 0x00FF)); \
} while (0)

#define MIDI_WRITE4(fh, val) do { \
    fprintf(fh, "%c%c%c%c", \
        (char) (((val) & 0xFF000000) >> 24), \
        (char) (((val) & 0x00FF0000) >> 16), \
        (char) (((val) & 0x0000FF00) >> 8), \
        (char) ( (val) & 0x000000FF)); \
} while (0)

/* only some message types have a data2 field */
#define TYPE_HAS_DATA2(status) (!(status >= 0xC0 && status <= 0xDF))

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
    return Pmf_New(PmfFile);
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
    return Pmf_New(PmfTrack);
}

void Pmf_FreeTrack(PmfTrack *track)
{
    PmfEvent *ev = track->head, *next;
    while (ev) {
        next = ev->next;
        Pmf_FreeEvent(ev);
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
        newTracks = Pmf_Malloc((file->trackCt + 1) * sizeof(PmfTrack *));
        memcpy(newTracks, file->tracks, file->trackCt * sizeof(PmfTrack *));
        newTracks[file->trackCt++] = track;
        AL.free(file->tracks);
        file->tracks = newTracks;
    } else {
        file->tracks = Pmf_Malloc(sizeof(PmfTrack *));
        file->tracks[0] = track;
        file->trackCt = 1;
    }
}

/* and event */
PmfEvent *Pmf_AllocEvent()
{
    return Pmf_New(PmfEvent);
}

void Pmf_FreeEvent(PmfEvent *event)
{
    if (event->meta) Pmf_FreeMeta(event->meta);
    AL.free(event);
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

/* meta-events have extra fields */
PmfMeta *Pmf_AllocMeta(uint32_t length)
{
    PmfMeta *ret = Pmf_Calloc(sizeof(PmfMeta) + length);
    ret->length = length;
    return ret;
}

void Pmf_FreeMeta(PmfMeta *meta)
{
    AL.free(meta);
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
    if (fread(magic, 1, 4, from) != 4) BAD_DATA;
    if (memcmp(magic, "MThd", 4)) BAD_DATA;

    file = Pmf_AllocFile();

    /* get the chunk size */
    MIDI_READ4(chunkSize, from);
    if (chunkSize != 6) BAD_DATA;

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
    uint8_t status;
    uint32_t chunkSize;
    uint32_t rd;

    /* make sure it's a track */
    if (fread(magic, 1, 4, from) != 4) BAD_DATA;
    if (memcmp(magic, "MTrk", 4)) BAD_DATA;

    track = Pmf_NewTrack(file);

    /* get the chunk size to be read */
    MIDI_READ4(chunkSize, from);

    /* and read it */
    status = 0;
    while (chunkSize > 0) {
        if ((perr = Pmf_ReadMidiEvent(track, from, &status, &rd))) return perr;
        if (rd > chunkSize) BAD_DATA;
        chunkSize -= rd;
    }

    return pmNoError;
}

static PmError Pmf_ReadMidiEvent(PmfTrack *track, FILE *from, uint8_t *pstatus, uint32_t *sz)
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
    rd++;

    /* hopefully it's a simple event */
    if (status < 0xF0) {
        if (status < 0x80) {
            /* status is from last event */
            data1 = status;
            status = *pstatus;
        } else {
            MIDI_READ1(data1, from); rd++;
        }

        /* not all have data2 (argh) */
        if (TYPE_HAS_DATA2(status)) {
            MIDI_READ1(data2, from); rd++;
        }

    } else if (status == 0xF0 || status == 0xF7 || status == 0xFF) { /* SysEx or meta */
        uint8_t mtype;
        uint32_t srd, length;
        PmfMeta *meta;

        /* meta type */
        if (status == 0xFF) { /* actual meta */
            MIDI_READ1(mtype, from);
            rd++;
        } else {
            mtype = status;
        }

        /* data length */
        if ((perr = Pmf_ReadMidiBignum(&length, from, &srd))) return perr;
        rd += srd;

        meta = Pmf_AllocMeta(length);
        meta->type = mtype;
        event->meta = meta;

        /* and the data itself */
        if (fread(meta->data, 1, length, from) != length) BAD_DATA;
        rd += length;

        /* carry over some data for convenience */
        data1 = data2 = 0;
        if (length >= 1) data1 = meta->data[0];
        if (length >= 2) data2 = meta->data[1];

    } else {
        fprintf(stderr, "Unrecognized MIDI event type %02X!\n", status);
        BAD_DATA;

    }

    event->e.message = Pm_Message(status, data1, data2);
    *pstatus = status;
    *sz = rd;
    return pmNoError;
}

static PmError Pmf_ReadMidiBignum(uint32_t *into, FILE *from, uint32_t *sz)
{
    uint32_t ret = 0;
    int more = 1;
    unsigned char cur;

    *sz = 0;
    while (more) {
        if (fread(&cur, 1, 1, from) != 1) BAD_DATA;
        (*sz)++;

        /* is there more? */
        if (cur & 0x80) {
            more = 1;
            cur &= 0x7F;
        } else {
            more = 0;
        }

        ret <<= 7;
        ret += cur;
    }

    *into = ret;
    return pmNoError;
}

/* write out a MIDI file */
PmError Pmf_WriteMidiFile(FILE *into, PmfFile *from)
{
    PmError perr;
    int i;

    if ((perr = Pmf_WriteMidiHeader(into, from))) return perr;

    for (i = 0; i < from->trackCt; i++) {
        if ((perr = Pmf_WriteMidiTrack(into, from->tracks[i]))) return perr;
    }

    return pmNoError;
}

static PmError Pmf_WriteMidiHeader(FILE *into, PmfFile *from)
{
    /* magic and chunk size */
    fwrite("MThd\0\0\0\x06", 1, 8, into);

    /* and the rest */
    MIDI_WRITE2(into, from->format);
    MIDI_WRITE2(into, from->trackCt);
    MIDI_WRITE2(into, from->timeDivision);

    return pmNoError;
}

static PmError Pmf_WriteMidiTrack(FILE *into, PmfTrack *track)
{
    PmError perr;
    PmfEvent *event;
    uint8_t status;
    uint32_t chunkSize;

    /* track header */
    fwrite("MTrk", 1, 4, into);

    /* get the chunk size to be written */
    chunkSize = 0;
    event = track->head;
    status = 0;
    while (event) {
        chunkSize += Pmf_GetMidiEventLength(event, &status);
        event = event->next;
    }
    MIDI_WRITE4(into, chunkSize);

    /* and write it */
    event = track->head;
    status = 0;
    while (event) {
        if ((perr = Pmf_WriteMidiEvent(into, event, &status))) return perr;
        event = event->next;
    }

    return pmNoError;
}

static PmError Pmf_WriteMidiEvent(FILE *into, PmfEvent *event, uint8_t *pstatus)
{
    PmError perr;
    uint8_t status, data1, data2;

    /* write the delta time */
    if ((perr = Pmf_WriteMidiBignum(into, event->deltaTm))) return perr;

    /* get out the parts */
    status = Pm_MessageStatus(event->e.message);
    data1 = Pm_MessageData1(event->e.message);
    data2 = Pm_MessageData2(event->e.message);

    /* hopefully it's a simple event */
    if (status < 0xF0) {
        /* write the status if we need to */
        if (status != *pstatus) {
            MIDI_WRITE1(into, status);
        }

        /* then write data1 */
        MIDI_WRITE1(into, data1);

        /* not all have data2 (argh) */
        if (TYPE_HAS_DATA2(status)) {
            MIDI_WRITE1(into, data2);
        }

    } else if (event->meta) { /* has metadata */
        PmfMeta *meta = event->meta;

        MIDI_WRITE1(into, status);

        /* meta type */
        if (status == 0xFF) { /* actual meta */
            MIDI_WRITE1(into, meta->type);
        }

        /* data length */
        if ((perr = Pmf_WriteMidiBignum(into, meta->length))) return perr;

        /* and the data itself */
        fwrite(meta->data, 1, meta->length, into);

    } else {
        fprintf(stderr, "Unrecognized MIDI event type %02X!\n", status);
        BAD_DATA;

    }

    *pstatus = status;
    return pmNoError;
}

static uint32_t Pmf_GetMidiEventLength(PmfEvent *event, uint8_t *pstatus)
{
    uint32_t sz = 0;
    uint8_t status;

    /* delta time */
    sz += Pmf_GetMidiBignumLength(event->deltaTm);

    /* get out the parts */
    status = Pm_MessageStatus(event->e.message);

    if (status < 0xF0) {
        /* write the status if we need to */
        if (status != *pstatus) sz++;

        /* then data1 */
        sz++;

        /* not all have data2 (argh) */
        if (TYPE_HAS_DATA2(status)) sz++;

    } else if (event->meta) { /* has metadata */
        PmfMeta *meta = event->meta;

        /* status */
        sz++;

        /* meta type */
        if (status == 0xFF) sz++;

        /* data length */
        sz += Pmf_GetMidiBignumLength(meta->length);

        /* and the data itself */
        sz += meta->length;

    } else {
        fprintf(stderr, "Unrecognized MIDI event type %02X!\n", status);
        BAD_DATA;

    }

    *pstatus = status;
    return sz;
}

static PmError Pmf_WriteMidiBignum(FILE *into, uint32_t val)
{
    unsigned char buf[5];
    int bufl, i;

    /* write it into the buf first */
    buf[0] = 0;
    for (bufl = 0; val > 0; bufl++) {
        buf[bufl] = (val & 0x7F);
        val >>= 7;
    }
    if (bufl == 0) bufl = 1;

    /* mark the high bits */
    for (i = 1; i < bufl; i++) {
        buf[i] |= 0x80;
    }

    /* then write it out */
    for (i = bufl - 1; i >= 0; i--) {
        MIDI_WRITE1(into, buf[i]);
    }

    return pmNoError;
}

static uint32_t Pmf_GetMidiBignumLength(uint32_t val)
{
    uint32_t sz = 1;
    val >>= 7;

    while (val > 0) {
        sz++;
        val >>= 7;
    }

    return sz;
}
