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

/* this is an X-header, define your own PFUNC and HS (if necessary) */

/* plugin initialization */
PFUNC(int, init, (HS))

/* called to handle arguments */
PFUNC(int, argHandler, (HS, int *, char **))

/* print a usage message */
PFUNC(int, usage, (HS))

/* function to call just before the main loop */
PFUNC(int, begin, (HS))

/* if you need to replace the main loop, provide mainLoop */
PFUNC(int, mainLoop, (HS))

/* called every tick, before figuring out where we are in the MIDI (so this can
 * change MIDI tick/tempo) */
PFUNC(int, tickPreMidi, (HS, PtTimestamp))

/* called every tick, after figuring out where we are in the MIDI, but only if
 * we're anywhere at all (nextTick >= 0) */
PFUNC(int, tickWithMidi, (HS, PtTimestamp, uint32_t))

/* called whenever a non-meta event is received. Return 1 to play the event, 0
 * to quash it. The last argument is whether to write the (presumably modified)
 * event to the output file, defaulting to 0 (no) */
PFUNC(int, handleEvent, (HS, PtTimestamp, uint32_t, int, MfEvent *, int *))

/* called whenever a meta event is received. Return 1 to handle the event (only
 * tempo events are meaningfully handled by humidity itself, but other plugins
 * may care about other events), 0 to quash it. The last argument is whether to
 * write the (presumably modified) event to the output file, defaulting to 0
 * (no); it is currently unsupported, but present for future support. */
PFUNC(int, handleMetaEvent, (HS, PtTimestamp, uint32_t, int, MfEvent *, int *))

/* if you need a special way to quit (exit(0) won't work), accept this function
 * and return 1 */
PFUNC(int, quit, (HS, int))
