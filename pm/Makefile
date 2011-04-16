CC=gcc
CFLAGS=-O2 -g $(ECFLAGS)
ECFLAGS=
LD=$(CC)
LDFLAGS=$(ELDFLAGS)
LIBS=-lportmidi
ELDFLAGS=

all: dumpfile

pmf/libportmidifile.a:
	cd pmf ; $(MAKE)

dumpfile: dumpfile.o pmf/libportmidifile.a
	$(LD) $(CFLAGS) $(LDFLAGS) $< pmf/libportmidifile.a $(LIBS) -o dumpfile

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o dumpfile
	cd pmf ; $(MAKE) clean
