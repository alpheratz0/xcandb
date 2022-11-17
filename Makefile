.POSIX:
.PHONY: all clean install uninstall dist

include config.mk

all: xpngcrop

xpngcrop: xpngcrop.o
	$(CC) $(LDFLAGS) -o xpngcrop xpngcrop.o $(LDLIBS)

clean:
	rm -f xpngcrop xpngcrop.o xpngcrop-$(VERSION).tar.gz

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f xpngcrop $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/xpngcrop
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	cp -f xpngcrop.1 $(DESTDIR)$(MANPREFIX)/man1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/xpngcrop.1

dist: clean
	mkdir -p xpngcrop-$(VERSION)
	cp -R COPYING config.mk Makefile README xpngcrop.1 xpngcrop.c xpngcrop-$(VERSION)
	tar -cf xpngcrop-$(VERSION).tar xpngcrop-$(VERSION)
	gzip xpngcrop-$(VERSION).tar
	rm -rf xpngcrop-$(VERSION)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/xpngcrop
	rm -f $(DESTDIR)$(MANPREFIX)/man1/xpngcrop.1
