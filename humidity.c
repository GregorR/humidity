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
static struct HumidityState hstate;

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
#define PA &hstate, __hplugin_i

/* should we just list devices and quit? */
static int listDevices = 0;

static int ready = 0;

/* functions */
void loadPlugin(void **plugin, char *bindir, char *pluginNm);
void usage();
void handler(PtTimestamp timestamp, void *ignore);

int main(int argc, char **argv)
{
    FILE *f;
    PmError perr;
    PtError pterr;
    MfFile *pf, *of;
    int argir, *argi, i;
    void *plugin;
    char *dir, *fil;

    whereAmI(argv[0], &dir, &fil);
    hstate.idev = hstate.odev = hstate.nextTick = -1;

    argi = &argir;
    for (argir = 1; argir < argc;) {
        char *arg = argv[argir];
        ARGN(p, plugin) {
            loadPlugin(&plugin, dir, argv[++argir]);
            argir++;
        } else {
            int ah = 0;
            PFUNC(ah, !ah, |=, argHandler, (PA, argi, argv));
            if (!ah)
                hostArg(argi, argv);
        }
    }

    PSF(perr, Pm_Initialize, ());
    PSF(perr, Mf_Initialize, ());
    PTSF(pterr, Pt_Start, (1, handler, NULL));

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
    if (hstate.odev == -1) {
        usage();
        exit(1);
    }

    /* check files */
    if (!hstate.ifile || !hstate.ofile) {
        usage();
        exit(1);
    }

    /* open it for input/output */
    if (hstate.idev >= 0) PSF(perr, Pm_OpenInput, (&hstate.idstream, hstate.idev, NULL, 1024, NULL, NULL));
    PSF(perr, Pm_OpenOutput, (&hstate.odstream, hstate.odev, NULL, 1024, NULL, NULL, 0));

    /* open the file for input */
    SF(f, fopen, NULL, (hstate.ifile, "rb"));

    /* and read it */
    PSF(perr, Mf_ReadMidiFile, (&pf, f));
    fclose(f);

    /* open everything in streaming mode */
    hstate.ifstream = Mf_OpenStream(pf);
    Mf_StartStream(hstate.ifstream, Pt_Time());

    of = Mf_NewFile(pf->timeDivision);
    hstate.ofstream = Mf_OpenStream(of);

    /* any plugin initialization */
    {
        int pinit = 1;
        PFUNC(pinit, pinit, &=, init, (PA));
        if (!pinit) exit(1);
    }

    ready = 1;

    while (1) Pt_Sleep(1<<30);

    return 0;
}

void hostArg(int *argi, char **argv)
{
    char *arg = argv[*argi];
    ARG(l, list) {
        listDevices = 1;

    } else ARGN(i, input-device) {
        hstate.idev = atoi(argv[++*argi]);

    } else ARGN(o, output-device) {
        hstate.odev = atoi(argv[++*argi]);

    } else if (arg[0] == '-') {
        usage();
        exit(1);

    } else if (!hstate.ifile) {
        hstate.ifile = arg;

    } else if (!hstate.ofile) {
        hstate.ofile = arg;

    } else {
        usage();
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

void loadPluginFuncs(void **plugin)
{
#define LFUNC(nm) hplugin[hplugins].nm = (hplugin_ ## nm ## _t) (size_t) dlsym(*plugin, #nm);
    LFUNC(argHandler);
    LFUNC(init);
#undef LFUNC
    hplugins++;
}

void loadPlugin(void **plugin, char *bindir, char *pluginNm)
{
    char pluginFn[PLUGIN_FN_LEN];

    snprintf(pluginFn, PLUGIN_FN_LEN, "%s/%s.so", bindir, pluginNm);
    if (tryLoadPlugin(plugin, pluginFn)) { loadPluginFuncs(plugin); return; }

    snprintf(pluginFn, PLUGIN_FN_LEN, "%s/../lib/humidity/%s.so", bindir, pluginNm);
    if (tryLoadPlugin(plugin, pluginFn)) { loadPluginFuncs(plugin); return; }

    fprintf(stderr, "Plugin %s not found!\n", pluginNm);
    exit(1);
}

void usage()
{
    fprintf(stderr, "Usage: humidity -i <input device> -o <output device> -p <plugin> [plugin options] <input file> <output file>\n"
                    "       humidity -l: List devices\n");
}

void handler(PtTimestamp timestamp, void *ignore)
{
    MfEvent *event;
    int rtrack;
    PmEvent ev;
    PtTimestamp ts;
    uint32_t tmTick;

    if (!ready) return;

    ts = Pt_Time();

    /* don't do anything if we shouldn't start yet */
    if (hstate.nextTick <= 0) return;

    /* figure out when to read to */
    tmTick = Mf_StreamGetTick(hstate.ifstream, ts);
    if (tmTick >= hstate.nextTick) tmTick = hstate.nextTick - 1;

    while (Mf_StreamReadUntil(hstate.ifstream, &event, &rtrack, 1, tmTick) == 1) {
        ev = event->e;

        if (event->meta) {
            if (event->meta->type == MIDI_M_TEMPO &&
                event->meta->length == MIDI_M_TEMPO_LENGTH) {
                PtTimestamp ts;
                unsigned char *data = event->meta->data;
                uint32_t tempo = (data[0] << 16) +
                    (data[1] << 8) +
                    data[2];
                Mf_StreamSetTempoTick(hstate.ifstream, &ts, event->absoluteTm, tempo);
            }

        } else {
            Pm_WriteShort(hstate.odstream, 0, ev.message);

        }

        Mf_FreeEvent(event);
    }

    if (Mf_StreamEmpty(hstate.ifstream) == TRUE) {
        MfFile *of;
        FILE *ofh;
        Mf_FreeFile(Mf_CloseStream(hstate.ifstream));
        of = Mf_CloseStream(hstate.ofstream);
        SF(ofh, fopen, NULL, (hstate.ofile, "wb"));
        Mf_WriteMidiFile(ofh, of);
        fclose(ofh);
        Mf_FreeFile(of);
        Pm_Terminate();

        exit(0);
    }
}
