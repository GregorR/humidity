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

PREFIX=/usr
PREFIX_BIN=$(PREFIX)/bin
PREFIX_PLUGINS=$(PREFIX)/lib/humidity

PROGRAMS=hdumpfile hdumpdev hreducevel htimesigfixer htemposmoother hmergemidis humidity
PLUGINS=mousebow.so notetapper.so play.so tempotapper.so
TARGETS=$(PROGRAMS) $(PLUGINS)

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
	( echo -n 'static const char *humidityVersion = "' ; hg id -i | tr -d '\n' ; echo '";' ) > hgid.h

install: $(TARGETS)
	mkdir -p $(PREFIX_BIN)
	install -s $(PROGRAMS) $(PREFIX_BIN)/
	mkdir -p $(PREFIX_PLUGINS)
	install -s $(PLUGINS) $(PREFIX_PLUGINS)/

clean:
	rm -f *.o $(TARGETS)
	cd midifile ; $(MAKE) clean
