CC=gcc
CFLAGS=-O2 -g $(ECFLAGS)
ECFLAGS=
AR=ar
ARFLAGS=rc
RANLIB=ranlib

PORTMIDIFILE_OS=portmidifile.o portmidifilealloc.o portmidifstream.o

all: libportmidifile.a

libportmidifile.a: $(PORTMIDIFILE_OS)
	$(AR) $(ARFLAGS) libportmidifile.a $(PORTMIDIFILE_OS)
	$(RANLIB) libportmidifile.a

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o libportmidifile.a
