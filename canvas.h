/*
	Copyright (C) 2022 <alpheratz99@protonmail.com>

	This program is free software; you can redistribute it and/or modify it under
	the terms of the GNU General Public License version 2 as published by the
	Free Software Foundation.

	This program is distributed in the hope that it will be useful, but WITHOUT ANY
	WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
	FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along with
	this program; if not, write to the Free Software Foundation, Inc., 59 Temple
	Place, Suite 330, Boston, MA 02111-1307 USA

*/

#ifndef __XCANDB_CANVAS_H__
#define __XCANDB_CANVAS_H__

#include <stdint.h>

struct canvas {
	int width, height;
	uint32_t *pixels;
};

extern struct canvas *
canvas_create(int width, int height);

extern struct canvas *
canvas_load(const char *path);

extern void
canvas_save(struct canvas *canvas, const char *path);

extern void
canvas_crop(struct canvas *canvas, int x, int y,
            int width, int height);

extern void
canvas_blur(struct canvas *canvas, int x, int y,
            int width, int height, int strength);

extern void
canvas_destroy(struct canvas *canvas);


#endif
