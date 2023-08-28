# Copyright (C) 2022 <alpheratz99@protonmail.com>
# This program is free software.

VERSION=0.2.0
CC=cc
INCS=-I/usr/X11R6/include -Iinclude
CFLAGS=-std=c99 -pedantic -Wall -Wextra -Os $(INCS) -DVERSION=\"$(VERSION)\"
LDLIBS=-lxcb -lxcb-image -lxcb-cursor -lxcb-keysyms -lxcb-icccm -lpng -lxcb-xkb
LDFLAGS=-L/usr/X11R6/lib -s
PREFIX=/usr/local
MANPREFIX=$(PREFIX)/share/man
