/*
	Copyright (C) 2022-2023 <alpheratz99@protonmail.com>

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

#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include "canvas.h"
#include "util.h"
#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "vendor/stb_image_write.h"

extern Canvas_t *
canvas_load(const char *path)
{
	int w, h;
	unsigned char *px;
	Canvas_t *c;

	px = stbi_load(path, &w, &h, NULL, 4);

	if (NULL == px)
		return NULL;

	c = xmalloc(sizeof(Canvas_t));

	c->px = px;
	c->width = w;
	c->height = h;

	return c;
}

extern void
canvas_save(Canvas_t *c, const char *path)
{
	stbi_write_png(path, c->width, c->height, 4,
		c->px, c->width * 4);
}

extern void
canvas_crop(Canvas_t *c, int x, int y, int w, int h)
{
	int dx, dy;
	unsigned char *crop_area;

	if (x < 0) w += x, x = 0;
	if (y < 0) h += y, y = 0;
	if (x + w >= c->width) w = c->width - x;
	if (y + h >= c->height) h = c->height - y;

	if (w < 1 || h < 1)
		return;

	crop_area = xmalloc(4*w*h);

	for (dy = 0; dy < h; ++dy) {
		for (dx = 0; dx < w; ++dx) {
			memcpy(
				&crop_area[(dy*w+dx)*4],
				&c->px[((y+dy)*c->width+x+dx)*4],
				4
			);
		}
	}

	free(c->px);

	c->px = crop_area;
	c->width = w;
	c->height = h;
}

extern void
canvas_blur(Canvas_t *c, int x, int y, int w, int h, int strength)
{
	int dx, dy;
	int pass;
	int numpx, r, g, b, kdx, kdy;
	unsigned char *blur_area, *blur_area_previous, *tmp;

	if (x < 0) w += x, x = 0;
	if (y < 0) h += y, y = 0;
	if (x + w >= c->width) w = c->width - x;
	if (y + h >= c->height) h = c->height - y;

	if (w < 1 || h < 1)
		return;

	blur_area          = xmalloc(4*w*h);
	blur_area_previous = xmalloc(4*w*h);

	for (dy = 0; dy < h; ++dy) {
		for (dx = 0; dx < w; ++dx) {
			memcpy(
				&blur_area[(dy*w+dx)*4],
				&c->px[((y+dy)*c->width+x+dx)*4],
				4
			);
		}
	}

	for (pass = 0; pass < strength; ++pass) {
		tmp = blur_area_previous;
		blur_area_previous = blur_area;
		blur_area = tmp;

		for (dy = 0; dy < h; ++dy) {
			for (dx = 0; dx < w; ++dx) {
				numpx = r = g = b = 0;
				for (kdy = -3; kdy < 4; ++kdy) {
					if ((dy+kdy) < 0 || (dy+kdy) >= h) continue;
					for (kdx = -3; kdx < 4; ++kdx) {
						if ((dx+kdx) < 0 || (dx+kdx) >= w) continue;
						r += blur_area_previous[((dy+kdy)*w+dx+kdx)*4];
						g += blur_area_previous[((dy+kdy)*w+dx+kdx)*4+1];
						b += blur_area_previous[((dy+kdy)*w+dx+kdx)*4+2];
						numpx++;
					}
				}
				blur_area[(dy*w+dx)*4] = r/numpx;
				blur_area[(dy*w+dx)*4+1] = g/numpx;
				blur_area[(dy*w+dx)*4+2] = b/numpx;
			}
		}
	}

	for (dy = 0; dy < h; ++dy) {
		for (dx = 0; dx < w; ++dx) {
			memcpy(
				&c->px[((y+dy)*c->width+x+dx)*4],
				&blur_area[(dy*w+dx)*4],
				4
			);
		}
	}

	free(blur_area);
	free(blur_area_previous);
}

extern void
canvas_free(Canvas_t *c)
{
	free(c->px);
	free(c);
}
