CC=gcc
CFLAGS=-O2 -g $(ECFLAGS)
ECFLAGS=
LD=$(CC)
LDFLAGS=$(ELDFLAGS)
LIBS=-lportmidi -lporttime -lm
ELDFLAGS=

all: dumpfile dumpdev retempofile tempotapper mergefiles

midifile/libmidifile.a:
	cd midifile ; $(MAKE)

dumpfile: dumpfile.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< midifile/libmidifile.a $(LIBS) -o $@

dumpdev: dumpdev.o
	$(LD) $(CFLAGS) $(LDFLAGS) $< $(LIBS) -o $@

retempofile: retempofile.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< midifile/libmidifile.a $(LIBS) -o $@

tempotapper: tempotapper.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< midifile/libmidifile.a $(LIBS) -o $@

mergefiles: mergefiles.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< midifile/libmidifile.a $(LIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o dumpfile dumpdev retempofile tempotapper mergefiles
	cd midifile ; $(MAKE) clean
