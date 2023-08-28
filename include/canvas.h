/*
	Copyright (C) 2022 <alpheratz99@protonmail.com>

	This program is free software; you can redistribute it and/or modify it
	under the terms of the GNU General Public License version 2 as published by
	the Free Software Foundation.

	This program is distributed in the hope that it will be useful, but WITHOUT
	ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
	FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
	more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc., 59
	Temple Place, Suite 330, Boston, MA 02111-1307 USA

*/

#ifndef __XCANDB_CANVAS_H__
#define __XCANDB_CANVAS_H__

typedef struct Canvas Canvas_t;
struct Canvas {
	int width, height;
	unsigned char *px;
};

extern Canvas_t *
canvas_load(const char *path);

extern void
canvas_save(Canvas_t *c, const char *path);

extern void
canvas_crop(Canvas_t *c, int x, int y, int w, int h);

extern void
canvas_blur(Canvas_t *c, int x, int y, int w, int h, int strength);

extern void
canvas_free(Canvas_t *c);

#endif
