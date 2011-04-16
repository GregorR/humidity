CC=gcc
CFLAGS=-O2 -g $(ECFLAGS)
ECFLAGS=
LD=$(CC)
LDFLAGS=$(ELDFLAGS)
LIBS=-lportmidi -lporttime
ELDFLAGS=

all: dumpfile playfile

pmf/libportmidifile.a:
	cd pmf ; $(MAKE)

dumpfile: dumpfile.o pmf/libportmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< pmf/libportmidifile.a $(LIBS) -o $@

playfile: playfile.o pmf/libportmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< pmf/libportmidifile.a $(LIBS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o dumpfile playfile
	cd pmf ; $(MAKE) clean
