/*
 * Copyright (C) 2012  Gregor Richards
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef HPLUGIN_H
#define HPLUGIN_H

#define HUMIDITY_MAX_PLUGINS 64

#include "midifile/midifstream.h"

/* the overall state of humidity */
struct HumidityState {
    /* input file stream */
    MfStream *ifstream;

    /* output file stream */
    MfStream *ofstream;

    /* input/output device IDs */
    PmDeviceID idev, odev;

    /* input and output device streams */
    PortMidiStream *idstream;
    PortMidiStream *odstream;

    /* input MIDI file to read */
    char *ifile;

    /* output MIDI file to write to */
    char *ofile;

    /* tick of the next note (note to wait for an event before playing) */
    int32_t nextTick;

    /* any plugin-specific state */
    void *pstate[HUMIDITY_MAX_PLUGINS];
};

/* plugin functions */
#ifdef IN_HUMIDITY_PLUGIN
#define PFUNC(type, nm, args) \
type nm args; \
typedef type (*hplugin_ ## nm ## _t) args;
#else
#define PFUNC(type, nm, args) \
typedef type (*hplugin_ ## nm ## _t) args;
#endif
#define HS struct HumidityState *, int

#include "hplugin_functions.h"

#undef HS
#undef PFUNC

struct HumidityPlugin {
#define PFUNC(type, nm, args) hplugin_ ## nm ## _t nm;
#include "hplugin_functions.h"
#undef PFUNC
};

#ifdef IN_HUMIDITY_PLUGIN
/* convenience macros for arguments
 * Use:
 * int somePluginFunction(HS)  (gives you struct HumidityState *hstate, int pnum)
 * {
 *     STATE;  (gives you struct PluginNameState *pstate)
 * }
 */
#define HS struct HumidityState *hstate, int pnum
#define HUMIDITY_PLUGIN_STATE(PluginName) struct PluginName ## State *pstate = (struct PluginName ## State *) hstate->pstate[pnum]
#define HUMIDITY_PLUGIN_STATE_PRIME(PluginName) HUMIDITY_PLUGIN_STATE(PluginName)
#define STATE HUMIDITY_PLUGIN_STATE_PRIME(IN_HUMIDITY_PLUGIN)
#endif

#endif
