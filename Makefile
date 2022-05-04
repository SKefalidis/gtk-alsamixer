# compile with gcc
CC=gcc

# set compiler options here.
CFLAGS = -lm -fpermissive

# set the name of the executable file to compile here
PROGRAM = gtk-alsamixer

INSTALL = /usr/bin/install -c
INSTALL_DATA = $(INSTALL) -m 644

all:
	$(CC) $(CFLAGS) `pkg-config gtk+-2.0 alsa --libs --cflags` src/*.c -o $(PROGRAM)

clean:
	rm -rf src/*o $(PROGRAM)
	
install : all
	$(INSTALL) -d $(DESTDIR)/usr/bin
	$(INSTALL) $(PROGRAM) $(DESTDIR)/usr/bin/$(PROGRAM)
