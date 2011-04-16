CC=gcc
CFLAGS=-O2 -g $(ECFLAGS)
ECFLAGS=
LD=$(CC)
LDFLAGS=$(ELDFLAGS)
LIBS=-lportmidi -lporttime -lm
ELDFLAGS=

all: dumpfile retempofile

midifile/libmidifile.a:
	cd midifile ; $(MAKE)

dumpfile: dumpfile.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< midifile/libmidifile.a $(LIBS) -o $@

retempofile: retempofile.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< midifile/libmidifile.a $(LIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o dumpfile retempofile
	cd midifile ; $(MAKE) clean
