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

#define _POSIX_C_SOURCE 200112L /* for vsnprintf */

#include <stdio.h>
#include <string.h>

#include "helpers.h"
#include "midifile/midi.h"
#include "miditag.h"

#ifndef va_copy
#ifdef __va_copy
#define va_copy __va_copy
#endif
#endif

int vmidiTagStream(MfStream *stream, const char *format, va_list ap)
{
    char *buf = NULL;
    int bufsz, written;
    MfEvent *event;
    va_list apc;

    /* write it to the buffer */
    written = 1;
    do {
        bufsz = written + 1;
        SF(buf, realloc, NULL, (buf, bufsz));
        va_copy(apc, ap);
        written = vsnprintf(buf, bufsz, format, apc);
        va_end(apc);
    } while (written >= bufsz);

    /* then write it to the file */
    event = Mf_NewEvent();
    event->e.message = Pm_Message(MIDI_STATUS_META, 0, 0);
    event->meta = Mf_NewMeta(written);
    event->meta->type = MIDI_M_TEXT;
    strcpy((char *) event->meta->data, buf);
    Mf_StreamWriteOne(stream, 0, event);

    /* get rid of our buffer */
    free(buf);

    return written;
}

int midiTagStream(MfStream *stream, const char *format, ...)
{
    va_list ap;
    int ret;
    va_start(ap, format);
    ret = vmidiTagStream(stream, format, ap);
    va_end(ap);
    return ret;
}
