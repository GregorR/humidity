CC=gcc
CFLAGS=-O2 -g $(ECFLAGS)
ECFLAGS=
LD=$(CC)
LDFLAGS=$(ELDFLAGS)
LIBS=-lportmidi -lporttime -lm
ELDFLAGS=

TARGETS=dumpfile dumpdev chgvel retempofile tempotapper notetapper temposmoother mergefiles

all: $(TARGETS)

midifile/libmidifile.a:
	cd midifile ; $(MAKE)

dumpfile: dumpfile.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< midifile/libmidifile.a $(LIBS) -o $@

dumpdev: dumpdev.o
	$(LD) $(CFLAGS) $(LDFLAGS) $< $(LIBS) -o $@

chgvel: chgvel.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< midifile/libmidifile.a $(LIBS) -o $@

retempofile: retempofile.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< midifile/libmidifile.a $(LIBS) -o $@

tempotapper: tempotapper.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< midifile/libmidifile.a $(LIBS) -o $@

notetapper: notetapper.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< midifile/libmidifile.a $(LIBS) -o $@

temposmoother: temposmoother.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< midifile/libmidifile.a $(LIBS) -o $@

mergefiles: mergefiles.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< midifile/libmidifile.a $(LIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGETS)
	cd midifile ; $(MAKE) clean
