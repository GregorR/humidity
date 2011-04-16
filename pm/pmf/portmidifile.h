#ifndef PORTMIDIFILE_H
#define PORTMIDIFILE_H

#include <stdio.h>
#include <stdlib.h>

#include "portmidi.h"

/* This is a MIDI file type handler designed to work with PortMidi. */

/* basic types */
typedef struct __PmfFile PmfFile;
typedef struct __PmfTrack PmfTrack;
typedef struct __PmfEvent PmfEvent;
typedef struct __PmfMeta PmfMeta;

/* initialization */
PmError Pmf_Initialize(void);

/* MIDI file */
struct __PmfFile {
    uint16_t format, expectedTracks, timeDivision;
    uint16_t trackCt;
    PmfTrack **tracks;
};
PmfFile *Pmf_AllocFile(void);
void Pmf_FreeFile(PmfFile *file);

/* track */
struct __PmfTrack {
    PmfEvent *head, *tail;
};
PmfTrack *Pmf_AllocTrack(void);
void Pmf_FreeTrack(PmfTrack *track);
PmfTrack *Pmf_NewTrack(PmfFile *file);
void Pmf_PushTrack(PmfFile *file, PmfTrack *track);

/* and event */
struct __PmfEvent {
    PmfEvent *next;
    uint32_t deltaTm, absoluteTm;
    PmEvent e;
    PmfMeta *meta;
};
PmfEvent *Pmf_AllocEvent(void);
void Pmf_FreeEvent(PmfEvent *event);
void Pmf_PushEvent(PmfTrack *track, PmfEvent *event);

/* meta-events have extra fields */
struct __PmfMeta {
    uint8_t type;
    uint32_t length;
    unsigned char data[1];
};
PmfMeta *Pmf_AllocMeta(uint32_t length);
void Pmf_FreeMeta(PmfMeta *meta);

/* read in a MIDI file */
PmError Pmf_ReadMidiFile(PmfFile **into, FILE *from);

/* write out a MIDI file */
PmError Pmf_WriteMidiFile(FILE *into, PmfFile *from);

#endif
