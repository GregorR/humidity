/*
 * Copyright (C) 2011, 2012  Gregor Richards
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

#define _POSIX_C_SOURCE 200112L /* for snprintf */

#include <math.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "args.h"
#include "helpers.h"
#include "hplugin.h"
#include "midifile/midi.h"
#include "midifile/midifstream.h"
#include "pmhelpers.h"
#include "whereami.h"

#define METRO_PER_QN 24
#define PLUGIN_FN_LEN 1024

/* our overall state */
static struct HumidityState globalHState;

/* our currently loaded plugin */
static int hplugins;
static struct HumidityPlugin hplugin[HUMIDITY_MAX_PLUGINS];
#define PFUNC(var, cond, comb, f, args) do { \
    int __hplugin_i; \
    for (__hplugin_i = 0; __hplugin_i < hplugins; __hplugin_i++) { \
        if (hplugin[__hplugin_i].f && cond) { \
            (var) comb hplugin[__hplugin_i].f args; \
        } \
    } \
} while (0)
#define PA hstate, __hplugin_i

/* should we just list devices and quit? */
static int listDevices = 0;

static int ready = 0;

/* functions */
void hostArg(struct HumidityState *hstate, int *argi, char **argv);
void loadPlugin(struct HumidityState *hstate, char *bindir, char *pluginNm);
void usage(struct HumidityState *hstate);
void handler(PtTimestamp timestamp, void *vphstate);

int main(int argc, char **argv)
{
    FILE *f;
    PmError perr;
    PtError pterr;
    MfFile *pf, *of;
    int argir, *argi, i;
    char *dir, *fil;
    struct HumidityState *hstate = &globalHState;

    whereAmI(argv[0], &dir, &fil);
    hstate->idev = hstate->odev = hstate->nextTick = -1;

    argi = &argir;
    for (argir = 1; argir < argc;) {
        char *arg = argv[argir];
        ARGN(p, plugin) {
            loadPlugin(hstate, dir, argv[++argir]);
            argir++;
        } else {
            int ah = 0;
            PFUNC(ah, !ah, |=, argHandler, (PA, argi, argv));
            if (!ah)
                hostArg(hstate, argi, argv);
        }
    }

    PSF(perr, Pm_Initialize, ());
    PSF(perr, Mf_Initialize, ());
    PTSF(pterr, Pt_Start, (1, handler, (void *) &hstate));

    /* list devices */
    if (listDevices) {
        int ct = Pm_CountDevices();
        PmDeviceID def = Pm_GetDefaultInputDeviceID();
        const PmDeviceInfo *devinf;

        for (i = 0; i < ct; i++) {
            devinf = Pm_GetDeviceInfo(i);
            printf("%d%s: %s%s %s\n", i, (def == i) ? "*" : "",
                (devinf->input) ? "I" : "",
                (devinf->output) ? "O" : "",
                devinf->name);
        }

        exit(0);
    }

    /* choose device */
    if (hstate->odev == -1) {
        usage(hstate);
        exit(1);
    }

    /* check files */
    if (!hstate->ifile || !hstate->ofile) {
        usage(hstate);
        exit(1);
    }

    /* open it for input/output */
    if (hstate->idev >= 0) PSF(perr, Pm_OpenInput, (&hstate->idstream, hstate->idev, NULL, 1024, NULL, NULL));
    PSF(perr, Pm_OpenOutput, (&hstate->odstream, hstate->odev, NULL, 1024, NULL, NULL, 0));

    /* open the file for input */
    SF(f, fopen, NULL, (hstate->ifile, "rb"));

    /* and read it */
    PSF(perr, Mf_ReadMidiFile, (&pf, f));
    fclose(f);

    /* open everything in streaming mode */
    hstate->ifstream = Mf_OpenStream(pf);
    Mf_StartStream(hstate->ifstream, Pt_Time());

    of = Mf_NewFile(pf->timeDivision);
    hstate->ofstream = Mf_OpenStream(of);

    /* any plugin initialization */
    {
        int pinit = 1;
        PFUNC(pinit, pinit, &=, begin, (PA));
        if (!pinit) exit(1);
    }

    ready = 1;

    /* do some sort of main loop */
    i = 0;
    PFUNC(i, !i, |=, mainLoop, (PA));
    if (!i) while (1) Pt_Sleep(1<<30);

    return 1; /* shouldn't get here */
}

void hostArg(struct HumidityState *hstate, int *argi, char **argv)
{
    char *arg = argv[*argi];
    ARG(l, list) {
        listDevices = 1;

    } else ARGN(i, input-device) {
        hstate->idev = atoi(argv[++*argi]);

    } else ARGN(o, output-device) {
        hstate->odev = atoi(argv[++*argi]);

    } else if (arg[0] == '-') {
        usage(hstate);
        exit(1);

    } else if (!hstate->ifile) {
        hstate->ifile = arg;

    } else if (!hstate->ofile) {
        hstate->ofile = arg;

    } else {
        usage(hstate);
        exit(1);

    }

    ++*argi;
}

int tryLoadPlugin(void **plugin, char *pluginFn)
{
    if (!access(pluginFn, F_OK)) {
        if ((*plugin = dlopen(pluginFn, RTLD_NOW|RTLD_LOCAL))) return 1;
        fprintf(stderr, "Error loading %s: %s\n", pluginFn, dlerror());
        exit(1);
    }
    return 0;
}

void loadPluginFuncs(struct HumidityState *hstate, void *plugin)
{
#define LFUNC(nm) hplugin[hplugins].nm = (hplugin_ ## nm ## _t) (size_t) dlsym(plugin, #nm);
    LFUNC(init);
    LFUNC(argHandler);
    LFUNC(usage);
    LFUNC(begin);
    LFUNC(mainLoop);
    LFUNC(tickPreMidi);
    LFUNC(tickWithMidi);
    LFUNC(handleEvent);
    LFUNC(quit);
#undef LFUNC

    if (hplugin[hplugins].init) {
        /* call its initializer */
        hplugin[hplugins].init(hstate, hplugins);
    }

    hplugins++;
}

void loadPlugin(struct HumidityState *hstate, char *bindir, char *pluginNm)
{
    char pluginFn[PLUGIN_FN_LEN];
    void *plugin;

    snprintf(pluginFn, PLUGIN_FN_LEN, "%s/%s.so", bindir, pluginNm);
    if (tryLoadPlugin(&plugin, pluginFn)) { loadPluginFuncs(hstate, plugin); return; }

    snprintf(pluginFn, PLUGIN_FN_LEN, "%s/../lib/humidity/%s.so", bindir, pluginNm);
    if (tryLoadPlugin(&plugin, pluginFn)) { loadPluginFuncs(hstate, plugin); return; }

    fprintf(stderr, "Plugin %s not found!\n", pluginNm);
    exit(1);
}

void usage(struct HumidityState *hstate)
{
    int pusage = 0;
    fprintf(stderr, "Usage: humidity -i <input device> -o <output device> -p <plugin> [plugin options] <input file> <output file>\n"
                    "       humidity -l: List devices\n");
    PFUNC(pusage, 1, |=, usage, (PA));
}

void handler(PtTimestamp timestamp, void *vphstate)
{
    MfEvent *event;
    int rtrack, tmpi;
    PmEvent ev;
    uint32_t tmTick;
    struct HumidityState *hstate = (struct HumidityState *) vphstate;

    if (!ready) return;

    /* call pre-MIDI stuff */
    tmpi = 1;
    PFUNC(tmpi, tmpi, &=, tickPreMidi, (PA, timestamp));
    if (!tmpi) return;

    /* don't do anything if we shouldn't start yet */
    if (hstate->nextTick <= 0) return;

    /* figure out when to read to */
    tmTick = Mf_StreamGetTick(hstate->ifstream, timestamp);
    if (tmTick >= hstate->nextTick) tmTick = hstate->nextTick - 1;

    /* now that we know where we are, tell the plugins */
    tmpi = 1;
    PFUNC(tmpi, tmpi, &=, tickWithMidi, (PA, timestamp, tmTick));
    if (!tmpi) return;

    while (Mf_StreamReadUntil(hstate->ifstream, &event, &rtrack, 1, tmTick) == 1) {
        ev = event->e;

        if (event->meta) {
            if (event->meta->type == MIDI_M_TEMPO &&
                event->meta->length == MIDI_M_TEMPO_LENGTH) {
                PtTimestamp ts;
                unsigned char *data = event->meta->data;
                uint32_t tempo = (data[0] << 16) +
                    (data[1] << 8) +
                    data[2];
                Mf_StreamSetTempoTick(hstate->ifstream, &ts, event->absoluteTm, tempo);
            }

        } else {
            /* perhaps a plugin will handle this event */
            tmpi = 1;
            PFUNC(tmpi, tmpi, &=, handleEvent, (PA, timestamp, tmTick, rtrack, event));
            if (tmpi)
                Pm_WriteShort(hstate->odstream, 0, ev.message);

        }

        Mf_FreeEvent(event);
    }

    if (Mf_StreamEmpty(hstate->ifstream) == TRUE) {
        MfFile *of;
        FILE *ofh;
        Mf_FreeFile(Mf_CloseStream(hstate->ifstream));
        of = Mf_CloseStream(hstate->ofstream);
        SF(ofh, fopen, NULL, (hstate->ofile, "wb"));
        Mf_WriteMidiFile(ofh, of);
        fclose(ofh);
        Mf_FreeFile(of);
        Pm_Terminate();
        ready = 0;

        /* quit somehow */
        tmpi = 0;
        PFUNC(tmpi, !tmpi, |=, quit, (PA, 0));
        if (!tmpi) exit(0);
    }
}
