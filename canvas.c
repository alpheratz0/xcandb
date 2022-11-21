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

#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <png.h>
#include <setjmp.h>
#include "canvas.h"
#include "util.h"

extern struct canvas *
canvas_create(int width, int height)
{
	struct canvas *canvas;

	canvas = malloc(sizeof(struct canvas));
	canvas->pixels = malloc(sizeof(uint32_t) * width * height);
	canvas->width = width;
	canvas->height = height;

	return canvas;
}

extern struct canvas *
canvas_load(const char *path)
{
	FILE *fp;
	png_struct *png;
	png_info *pnginfo;
	png_byte **rows, bit_depth;
	int16_t x, y;
	struct canvas *canvas;

	if (NULL == (fp = fopen(path, "rb")))
		die("failed to open file %s: %s", path, strerror(errno));

	if (NULL == (png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL)))
		die("png_create_read_struct failed");

	if (NULL == (pnginfo = png_create_info_struct(png)))
		die("png_create_info_struct failed");

	if (setjmp(png_jmpbuf(png)) != 0)
		die("aborting due to libpng error");

	png_init_io(png, fp);
	png_read_info(png, pnginfo);
	canvas = canvas_create(png_get_image_width(png, pnginfo), png_get_image_height(png, pnginfo));

	bit_depth = png_get_bit_depth(png, pnginfo);

	png_set_interlace_handling(png);

	if (bit_depth == 16)
		png_set_strip_16(png);

	if (png_get_valid(png, pnginfo, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);

	switch (png_get_color_type(png, pnginfo)) {
		case PNG_COLOR_TYPE_RGB:
			png_set_filler(png, 0xff, PNG_FILLER_AFTER);
			break;
		case PNG_COLOR_TYPE_PALETTE:
			png_set_palette_to_rgb(png);
			png_set_filler(png, 0xff, PNG_FILLER_AFTER);
			break;
		case PNG_COLOR_TYPE_GRAY:
			if (bit_depth < 8)
				png_set_expand_gray_1_2_4_to_8(png);
			png_set_filler(png, 0xff, PNG_FILLER_AFTER);
			png_set_gray_to_rgb(png);
			break;
		case PNG_COLOR_TYPE_GRAY_ALPHA:
			png_set_gray_to_rgb(png);
	}

	png_read_update_info(png, pnginfo);

	rows = png_malloc(png, sizeof(png_byte *) * canvas->height);

	for (y = 0; y < canvas->height; ++y)
		rows[y] = png_malloc(png, png_get_rowbytes(png, pnginfo));

	png_read_image(png, rows);

	for (y = 0; y < canvas->height; ++y) {
		for (x = 0; x < canvas->width; ++x) {
			if (rows[y][x*4+3] == 0)
				canvas->pixels[y*canvas->width+x] = 0xffffff;
			else canvas->pixels[y*canvas->width+x] = rows[y][x*4+0] << 16 |
				rows[y][x*4+1] << 8 |
				rows[y][x*4+2];
		}
		png_free(png, rows[y]);
	}

	png_free(png, rows);
	png_read_end(png, NULL);
	png_free_data(png, pnginfo, PNG_FREE_ALL, -1);
	png_destroy_read_struct(&png, NULL, NULL);
	fclose(fp);

	return canvas;
}

extern void
canvas_save(struct canvas *canvas, const char *path)
{
	int x, y;
	FILE *fp;
	png_struct *png;
	png_info *pnginfo;
	png_byte *row;

	if (NULL == (fp = fopen(path, "wb")))
		die("fopen failed: %s", strerror(errno));

	if (NULL == (png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL)))
		die("png_create_write_struct failed");

	if (NULL == (pnginfo = png_create_info_struct(png)))
		die("png_create_info_struct failed");

	if (setjmp(png_jmpbuf(png)) != 0)
		die("aborting due to libpng error");

	png_init_io(png, fp);

	png_set_IHDR(
		png, pnginfo, canvas->width, canvas->height, 8, PNG_COLOR_TYPE_RGB,
		PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE
	);

	png_write_info(png, pnginfo);
	png_set_compression_level(png, 3);

	row = malloc(canvas->width * 3);

	for (y = 0; y < canvas->height; ++y) {
		for (x = 0; x < canvas->width; ++x) {
			row[x*3+0] = (canvas->pixels[y*canvas->width+x] & 0xff0000) >> 16;
			row[x*3+1] = (canvas->pixels[y*canvas->width+x] & 0xff00) >> 8;
			row[x*3+2] = canvas->pixels[y*canvas->width+x] & 0xff;
		}
		png_write_row(png, row);
	}

	png_write_end(png, NULL);
	png_free_data(png, pnginfo, PNG_FREE_ALL, -1);
	png_destroy_write_struct(&png, NULL);
	fclose(fp);
	free(row);
}

extern void
canvas_crop(struct canvas *canvas, int x, int y,
            int width, int height)
{
	int dx, dy;
	uint32_t *crop_area;

	if (x < 0) { width  += x, x = 0; }
	if (y < 0) { height += y, y = 0; }
	if (x + width >= canvas->width) width = canvas->width - x;
	if (y + height >= canvas->height) height = canvas->height - y;

	if (width < 1 || height < 1)
		return;

	crop_area = malloc(sizeof(uint32_t) * width * height);

	for (dy = 0; dy < height; ++dy)
		for (dx = 0; dx < width; ++dx)
			crop_area[dy*width+dx] = canvas->pixels[(y+dy)*canvas->width+x+dx];

	free(canvas->pixels);

	canvas->pixels = crop_area;
	canvas->width = width;
	canvas->height = height;
}

extern void
canvas_blur(struct canvas *canvas, int x, int y,
            int width, int height, int strength)
{
	int dx, dy;
	int pass;
	int numpx, r, g, b, kdx, kdy;
	uint32_t *blur_area, *blur_area_previous, *tmp;

	if (x < 0) { width  += x; x = 0; }
	if (y < 0) { height += y; y = 0; }
	if (x + width >= canvas->width) width = canvas->width - x;
	if (y + height >= canvas->height) height = canvas->height - y;

	if (width < 1 || height < 1)
		return;

	blur_area          = malloc(sizeof(uint32_t) * width * height);
	blur_area_previous = malloc(sizeof(uint32_t) * width * height);

	for (dy = 0; dy < height; ++dy)
		for (dx = 0; dx < width; ++dx)
			blur_area_previous[dy*width+dx] =
				blur_area[dy*width+dx] =
					canvas->pixels[(y+dy)*canvas->width+x+dx];

	for (pass = 0; pass < strength; ++pass) {
		tmp = blur_area_previous;
		blur_area_previous = blur_area;
		blur_area = tmp;

		for (dy = 0; dy < height; ++dy) {
			for (dx = 0; dx < width; ++dx) {
				numpx = r = g = b = 0;
				for (kdy = -3; kdy < 4; ++kdy) {
					if ((dy+kdy) < 0 || (dy+kdy) >= height)
						continue;
					for (kdx = -3; kdx < 4; ++kdx) {
						if ((dx+kdx) < 0 || (dx+kdx) >= width)
							continue;

						r += (blur_area_previous[(dy+kdy)*width+dx+kdx] >> 16) & 0xff;
						g += (blur_area_previous[(dy+kdy)*width+dx+kdx] >> 8) & 0xff;
						b += (blur_area_previous[(dy+kdy)*width+dx+kdx] >> 0) & 0xff;
						numpx++;
					}
				}
				blur_area[dy*width+dx] = ((r/numpx) << 16) | ((g/numpx) << 8) | (b/numpx);
			}
		}
	}

	for (dy = 0; dy < height; ++dy)
		for (dx = 0; dx < width; ++dx)
			canvas->pixels[(y+dy)*canvas->width+x+dx] = blur_area[dy*width+dx];

	free(blur_area);
	free(blur_area_previous);
}

extern void
canvas_destroy(struct canvas *canvas)
{
	free(canvas->pixels);
	free(canvas);
}
