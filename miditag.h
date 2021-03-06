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

#ifndef MIDITAG_H
#define MIDITAG_H

#include <stdarg.h>
#include "midifile/midifstream.h"

/* tag a MIDI stream in *printf style */
int vmidiTagStream(MfStream *stream, const char *format, va_list ap);
int midiTagStream(MfStream *stream, const char *format, ...);

/* tag a MIDI stream with our generic header, pre and post optional (which go
 * pre- and post- version) */
int midiTagStreamHeader(MfStream *stream, const char *pre, const char *post);

/* tag a MIDI stream with our generic footer */
int midiTagStreamFooter(MfStream *stream);

#endif
