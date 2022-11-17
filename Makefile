.POSIX:
.PHONY: all clean install uninstall dist

include config.mk

all: xcranbl

xcranbl: xcranbl.o
	$(CC) $(LDFLAGS) -o xcranbl xcranbl.o $(LDLIBS)

clean:
	rm -f xcranbl xcranbl.o xcranbl-$(VERSION).tar.gz

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f xcranbl $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/xcranbl
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	cp -f xcranbl.1 $(DESTDIR)$(MANPREFIX)/man1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/xcranbl.1

dist: clean
	mkdir -p xcranbl-$(VERSION)
	cp -R COPYING config.mk Makefile README xcranbl.1 xcranbl.c xcranbl-$(VERSION)
	tar -cf xcranbl-$(VERSION).tar xcranbl-$(VERSION)
	gzip xcranbl-$(VERSION).tar
	rm -rf xcranbl-$(VERSION)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/xcranbl
	rm -f $(DESTDIR)$(MANPREFIX)/man1/xcranbl.1
