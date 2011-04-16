#ifndef PORTMIDIFSTREAM_H
#define PORTMIDIFSTREAM_H

#include "portmidifile.h"
#include "porttime.h"

/* types */
typedef struct __PmfStream PmfStream;

/* an active filestream */
struct __PmfStream {
    PmfFile *file;

    /* info for calculating tempo: timestamp when tempo was last changed, tick
     * when tempo was last changed, and new tempo */
    PtTimestamp tempoTs;
    int tempoUs; /* microseconds */
    uint32_t tempoTick, tempo;
};

/* open a stream for a file */
PmfStream *Pmf_OpenStream(PmfFile *of);

/* start a stream at this timestamp */
PmError Pmf_StartStream(PmfStream *stream, PtTimestamp timestamp);

/* close a stream, returning the now-complete file if you were writing (also
 * adds TrkEnd events and sets the format) */
PmfFile *Pmf_CloseStream(PmfStream *stream);

/* poll for events from the stream */
PmError Pmf_StreamPoll(PmfStream *stream);

/* is the stream empty? */
PmError Pmf_StreamEmpty(PmfStream *stream);

/* read events from the stream (loses ownership of events) */
int Pmf_StreamRead(PmfStream *stream, PmfEvent **into, int *track, int32_t length);

/* write events into the stream (takes ownership of events) */
PmError Pmf_StreamWrite(PmfStream *stream, int track, PmfEvent **events, int32_t length);
PmError Pmf_StreamWriteOne(PmfStream *stream, int track, PmfEvent *event);

/* get the current tempo from this filestream */
uint32_t Pmf_StreamGetTempo(PmfStream *stream);

/* get a tick from this filestream at a given timestamp */
uint32_t Pmf_StreamGetTick(PmfStream *stream, PtTimestamp timestamp);

/* get a timestamp from this filestream at a given tick */
PtTimestamp Pmf_StreamGetTimestamp(PmfStream *stream, int *us, uint32_t tick);

/* update the tempo for this filestream at a tick, writes the timestamp of the update into ts */
PmError Pmf_StreamSetTempoTick(PmfStream *stream, PtTimestamp *ts, uint32_t tick, uint32_t tempo);

/* update the tempo for this filestream at a timestamp, writes the tick of the update into tick */
PmError Pmf_StreamSetTempoTimestamp(PmfStream *stream, uint32_t *tick, PtTimestamp ts, uint32_t tempo);

#endif
