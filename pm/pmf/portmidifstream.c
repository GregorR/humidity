#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "portmidifstream.h"

#include "portmidifile.h"
#include "portmidifilealloc.h"

/* file-local miscellany */
static void Pmf_FinalizeTrack(PmfTrack *track);
static PmfTrack *Pmf_AssertTrack(PmfFile *file, int track);

/* open a stream for a file */
PmfStream *Pmf_OpenStream(PmfFile *of)
{
    PmfStream *ret = Pmf_New(PmfStream);
    ret->file = of;
    return ret;
}

/* start a stream at this timestamp */
PmError Pmf_StartStream(PmfStream *stream, PtTimestamp timestamp)
{
    stream->tempoTs = timestamp;
    stream->tempoUs = 0;
    stream->tempoTick = 0;
    stream->tempo = 500000; /* 120BPM = 500K us/qn */
    return pmNoError;
}

/* close a stream, returning the now-complete file if you were writing (also
 * adds TrkEnd events and sets the format) */
PmfFile *Pmf_CloseStream(PmfStream *stream)
{
    int i;
    PmfFile *file = stream->file;
    AL.free(stream);

    /* finalize all the tracks */
    for (i = 0; i < file->trackCt; i++) {
        Pmf_FinalizeTrack(file->tracks[i]);
    }

    /* and mark the format */
    file->format = (file->trackCt > 1) ? 1 : 0;

    return file;
}

static void Pmf_FinalizeTrack(PmfTrack *track)
{
    PmfEvent *event;
    int mustFinalize = 0;

    if (track->tail) {
        event = track->tail;
        if (!(event->meta) || event->meta->type != 0x2F) { /* last isn't end-of-stream */
            mustFinalize = 1;
        }
    } else {
        mustFinalize = 1;
    }

    if (mustFinalize) {
        /* OK, we have to finalize */
        event = Pmf_AllocEvent();
        event->e.message = Pm_Message(0xFF, 0, 0);
        event->meta = Pmf_AllocMeta(0);
        event->meta->type = 0x2F;
        Pmf_PushEvent(track, event);
    }
}

/* poll for events from the stream */
PmError Pmf_StreamPoll(PmfStream *stream)
{
    int i;
    PmfFile *file;
    PmfTrack *track;
    uint32_t curTick;

    /* calculate the current tick */
    curTick = Pmf_StreamGetTick(stream, Pt_Time());

    file = stream->file;
    for (i = 0; i < file->trackCt; i++) {
        track = file->tracks[i];
        if (track->head && track->head->absoluteTm <= curTick) return TRUE;
    }

    return FALSE;
}

/* is the stream empty? */
PmError Pmf_StreamEmpty(PmfStream *stream)
{
    int i;
    PmfFile *file;
    PmfTrack *track;

    file = stream->file;
    for (i = 0; i < file->trackCt; i++) {
        track = file->tracks[i];
        if (track->head) return FALSE;
    }

    return TRUE;
}

/* read events from the stream (loses ownership of events) */
int Pmf_StreamRead(PmfStream *stream, PmfEvent **into, int *ptrack, int32_t length)
{
    int rd = 0, i;
    PmfFile *file;
    PmfTrack *track;
    uint32_t curTick;

    /* calculate the current tick */
    curTick = Pmf_StreamGetTick(stream, Pt_Time());

    file = stream->file;
    for (i = 0; i < file->trackCt; i++) {
        track = file->tracks[i];
        if (track->head && track->head->absoluteTm <= curTick) {
            /* read in this one */
            into[rd] = track->head;
            ptrack[rd] = i;
            track->head->e.timestamp = Pmf_StreamGetTimestamp(stream, NULL, track->head->absoluteTm);
            track->head = track->head->next;
            if (!(track->head)) track->tail = NULL;
            rd++;

            /* stop if we're out of room */
            if (rd >= length) break;
        }
    }

    return rd;
}

/* write events into the stream (takes ownership of events) */
PmError Pmf_StreamWrite(PmfStream *stream, int track, PmfEvent **events, int32_t length)
{
    int i;
    PmError perr;

    for (i = 0; i < length; i++) {
        if ((perr = Pmf_StreamWriteOne(stream, track, events[i]))) return perr;
    }

    return pmNoError;
}

PmError Pmf_StreamWriteOne(PmfStream *stream, int trackno, PmfEvent *event)
{
    PmfTrack *track = Pmf_AssertTrack(stream->file, trackno);

    /* first correct the event's delta time */
    if (event->deltaTm == 0) {
        if (event->absoluteTm == 0 && event->e.timestamp != 0) {
            /* OK, convert back the timestamp */
            event->absoluteTm = Pmf_StreamGetTick(stream, event->e.timestamp);
        }

        if (event->absoluteTm != 0) {
            /* subtract away the delta */
            if (track->tail) {
                event->deltaTm = event->absoluteTm - track->tail->absoluteTm;
            } else {
                event->deltaTm = event->absoluteTm;
            }
        }
    }

    /* then add it */
    Pmf_PushEvent(track, event);
    return pmNoError;
}

static PmfTrack *Pmf_AssertTrack(PmfFile *file, int track)
{
    while (file->trackCt <= track) Pmf_NewTrack(file);
    return file->tracks[track];
}

/* get the current tempo from this filestream */
uint32_t Pmf_StreamGetTempo(PmfStream *stream)
{
    return stream->tempo;
}

/* get a tick from this filestream */
uint32_t Pmf_StreamGetTick(PmfStream *stream, PtTimestamp timestamp)
{
    uint64_t tsus = 0;

    /* adjust for the current tick */
    timestamp -= stream->tempoTs;
    if (stream->tempoUs > 0) {
        timestamp--;
        tsus = 1000 - stream->tempoUs;
    }
    tsus += (uint64_t) timestamp * 1000;

    /* tempo is in microseconds per quarter note, ticks are timeDivision per
     * quarter note, so the algo is:
     * tsus / tempo * timeDivision */
    return stream->tempoTick + tsus * stream->file->timeDivision / stream->tempo;
}

/* get a timestamp from this filestream at a given tick */
PtTimestamp Pmf_StreamGetTimestamp(PmfStream *stream, int *us, uint32_t tick)
{
    uint64_t tickl = tick - stream->tempoTick;
    uint64_t tsbase = ((uint64_t) stream->tempoTs * 1000) + stream->tempoUs;

    tsbase = tsbase + tickl * stream->tempo / stream->file->timeDivision;

    if (us) *us = tsbase % 1000;
    tsbase /= 1000;

    return tsbase;
}

/* update the tempo for this filestream at a tick, writes the timestamp of the update into ts */
PmError Pmf_StreamSetTempoTick(PmfStream *stream, PtTimestamp *ts, uint32_t tick, uint32_t tempo)
{
    int us;
    *ts = Pmf_StreamGetTimestamp(stream, &us, tick);
    stream->tempoTs = *ts;
    stream->tempoUs = us;
    stream->tempoTick = tick;
    stream->tempo = tempo;
    return pmNoError;
}

/* update the tempo for this filestream at a timestamp, writes the tick of the update into tick */
PmError Pmf_StreamSetTempoTimestamp(PmfStream *stream, uint32_t *tick, PtTimestamp ts, uint32_t tempo)
{
    *tick = Pmf_StreamGetTick(stream, ts);
    stream->tempoTs = ts;
    stream->tempoUs = 0;
    stream->tempoTick = *tick;
    stream->tempo = tempo;
    return pmNoError;
}
