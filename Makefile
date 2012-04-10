CC=gcc
CFLAGS=-O2 -g -fPIC $(ECFLAGS)
ECFLAGS=
LD=$(CC)
LDFLAGS=$(ELDFLAGS) -Lmidifile
SHFLAGS=-shared
LIBS=-lportmidi -lporttime -lm
MIDIFILE_LIBS=-lmidifile
SDL_LIBS=-lSDL
ELDFLAGS=

TARGETS=dumpfile dumpdev chgvel timesigfixer temposmoother mergefiles humidity \
    play.so mousebow.so notetapper.so

all: $(TARGETS)

midifile/libmidifile.a:
	cd midifile ; $(MAKE) ECFLAGS="$(CFLAGS)"

# Cancel GNU make's builtin rule
%: %.c

%: %.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< $(MIDIFILE_LIBS) $(LIBS) -o $@

humidity: humidity.o whereami.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< whereami.o $(MIDIFILE_LIBS) $(LIBS) -o $@

dumpdev: dumpdev.o
	$(LD) $(CFLAGS) $(LDFLAGS) $< $(LIBS) -o $@

%.so: %-sdl.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $(SHFLAGS) $< $(MIDIFILE_LIBS) $(LIBS) $(SDL_LIBS) -o $@

%.so: %.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $(SHFLAGS) $< $(MIDIFILE_LIBS) $(LIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(TARGETS)
	cd midifile ; $(MAKE) clean
