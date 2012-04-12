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

%: %.o miditag.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< miditag.o $(MIDIFILE_LIBS) $(LIBS) -o $@

humidity: humidity.o miditag.o whereami.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< miditag.o whereami.o $(MIDIFILE_LIBS) $(LIBS) -o $@

dumpdev: dumpdev.o
	$(LD) $(CFLAGS) $(LDFLAGS) $< $(LIBS) -o $@

%.so: %-sdl.o miditag.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $(SHFLAGS) $< miditag.o $(MIDIFILE_LIBS) $(LIBS) $(SDL_LIBS) -o $@

%.so: %.o miditag.o midifile/libmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $(SHFLAGS) $< miditag.o $(MIDIFILE_LIBS) $(LIBS) -o $@

%.o: %.c hgid.h
	$(CC) $(CFLAGS) -c $< -o $@

# ID file used for version specification
hgid.h: .hg/dirstate
	( echo -n 'const char *humidityVersion = "' ; hg id -i | tr -d '\n' ; echo '";' ) > hgid.h

clean:
	rm -f *.o $(TARGETS)
	cd midifile ; $(MAKE) clean
