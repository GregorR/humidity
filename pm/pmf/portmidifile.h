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
typedef struct __PmfAllocators PmfAllocators;

/* pluggable allocators */
struct __PmfAllocators {
    void *(*malloc)(size_t);
    const char *(*strerror)();
    void (*free)(void *);
};
extern PmfAllocators Pmf_Allocators;

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
};
PmfEvent *Pmf_AllocEvent(void);
void Pmf_FreeEvent(PmfEvent *event);
PmfEvent *Pmf_NewEvent(PmfTrack *track);
void Pmf_PushEvent(PmfTrack *track, PmfEvent *event);

/* read in a MIDI file */
PmError Pmf_ReadMidiFile(PmfFile **into, FILE *from);

/* write out a MIDI file */
PmError Pmf_WriteMidiFile(FILE *into, PmfFile *from);

#endif
