.POSIX:
.PHONY: all clean install uninstall dist

include config.mk

all: xcandb

xcandb: xcandb.o
	$(CC) $(LDFLAGS) -o xcandb xcandb.o $(LDLIBS)

clean:
	rm -f xcandb xcandb.o xcandb-$(VERSION).tar.gz

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f xcandb $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/xcandb
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	cp -f xcandb.1 $(DESTDIR)$(MANPREFIX)/man1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/xcandb.1

dist: clean
	mkdir -p xcandb-$(VERSION)
	cp -R COPYING config.mk Makefile README xcandb.1 xcandb.c xcandb-$(VERSION)
	tar -cf xcandb-$(VERSION).tar xcandb-$(VERSION)
	gzip xcandb-$(VERSION).tar
	rm -rf xcandb-$(VERSION)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/xcandb
	rm -f $(DESTDIR)$(MANPREFIX)/man1/xcandb.1
