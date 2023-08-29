.POSIX:
.PHONY: all clean install uninstall dist

include config.mk

OBJ=\
	src/xcandb.o \
	src/canvas.o \
	src/log.o \
	src/stb.o \
	src/utils.o

all: xcandb

xcandb: $(OBJ)
	$(CC) $(LDFLAGS) -o xcandb $(OBJ) $(LDLIBS)

clean:
	rm -f xcandb $(OBJ) xcandb-$(VERSION).tar.gz

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f xcandb $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/xcandb
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	cp -f xcandb.1 $(DESTDIR)$(MANPREFIX)/man1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/xcandb.1

dist: clean
	mkdir -p xcandb-$(VERSION)
	cp -R COPYING config.mk Makefile README xcandb.1 src include \
		xcandb-$(VERSION)
	tar -cf xcandb-$(VERSION).tar xcandb-$(VERSION)
	gzip xcandb-$(VERSION).tar
	rm -rf xcandb-$(VERSION)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/xcandb
	rm -f $(DESTDIR)$(MANPREFIX)/man1/xcandb.1
