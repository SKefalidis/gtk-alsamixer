INSTALL = /usr/bin/install -c
INSTALL_DATA = $(INSTALL) -m 644

all:
	g++ -fpermissive `pkg-config gtk+-2.0 alsa --libs --cflags` src/*.c -o gtk-alsamixer

clean:
	rm -rf src/*o gtk-alsamixer
	
install : all
	$(INSTALL) -d $(DESTDIR)/usr/bin
	$(INSTALL) gtk-alsamixer $(DESTDIR)/usr/bin/gtk-alsamixer
