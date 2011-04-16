CC=gcc
CFLAGS=-O2 -g $(ECFLAGS)
ECFLAGS=
AR=ar
ARFLAGS=rc
RANLIB=ranlib

all: libportmidifile.a

libportmidifile.a: portmidifile.o
	$(AR) $(ARFLAGS) libportmidifile.a $<
	$(RANLIB) libportmidifile.a

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o libportmidifile.a
