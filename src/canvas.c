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
#include <stdint.h>
#include <stdlib.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_image.h>
#include <xcb/shm.h>
#include <sys/shm.h>

#include "stb/stb_image.h"
#include "stb/stb_image_write.h"
#include "canvas.h"
#include "utils.h"
#include "log.h"

#define RED(c) ((c>>16) & 0xff)
#define GREEN(c) ((c>>8) & 0xff)
#define BLUE(c) ((c>>0) & 0xff)

struct Canvas {
	struct {
		int x;
		int y;
	} pos;

	int viewport_width;
	int viewport_height;

	int width;
	int height;
	uint32_t *px;

	/* X11 */
	xcb_connection_t *conn;
	xcb_screen_t *scr;
	xcb_window_t win;
	int shm;
	xcb_gcontext_t gc;
	union {
		struct {
			int id;
			xcb_shm_seg_t seg;
			xcb_pixmap_t pixmap;
		} shm;
		xcb_image_t *image;
	} x;
};

static inline uint32_t
__pack_color(unsigned char *p)
{
	return (uint32_t)(p[0]<<16)|(p[1]<<8)|p[2];
}

static inline void
__unpack_color(uint32_t c, unsigned char *p)
{
	p[0] = (c >> 16) & 0xff;
	p[1] = (c >>  8) & 0xff;
	p[2] = (c >>  0) & 0xff;
	p[3] = 0xff;
}

static int
__x_check_mit_shm_extension(xcb_connection_t *conn)
{
	xcb_generic_error_t *error;
	xcb_shm_query_version_cookie_t cookie;
	xcb_shm_query_version_reply_t *reply;

	cookie = xcb_shm_query_version(conn);
	reply = xcb_shm_query_version_reply(conn, cookie, &error);

	if (NULL != error) {
		if (NULL != reply)
			free(reply);
		free(error);
		return 0;
	}

	if (NULL != reply) {
		if (reply->shared_pixmaps == 0) {
			free(reply);
			return 0;
		}
		free(reply);
		return 1;
	}

	return 0;
}

static void
__canvas_set_size(Canvas_t *c, int w, int h)
{
	c->width = w;
	c->height = h;

	if (c->shm) {
		if (c->px) {
			shmctl(c->x.shm.id, IPC_RMID, NULL);
			xcb_shm_detach(c->conn, c->x.shm.seg);
			shmdt(c->px);
			xcb_free_pixmap(c->conn, c->x.shm.pixmap);
		}

		c->x.shm.seg = xcb_generate_id(c->conn);
		c->x.shm.pixmap = xcb_generate_id(c->conn);
		c->x.shm.id = shmget(IPC_PRIVATE, w*h*4, IPC_CREAT | 0600);

		if (c->x.shm.id < 0)
			die("shmget:");

		c->px = shmat(c->x.shm.id, NULL, 0);

		if (c->px == (void *) -1) {
			shmctl(c->x.shm.id, IPC_RMID, NULL);
			die("shmat:");
		}

		xcb_shm_attach(c->conn, c->x.shm.seg, c->x.shm.id, 0);
		shmctl(c->x.shm.id, IPC_RMID, NULL);

		xcb_shm_create_pixmap(c->conn, c->x.shm.pixmap, c->win, w, h,
				c->scr->root_depth, c->x.shm.seg, 0);
	} else {
		if (c->px)
			xcb_image_destroy(c->x.image);

		// FIXME: split source image into
		//        multiple xcb_image_t objects
		if (w*h*4 > 16*1024*1024 /* 16mb */)
			die("image too big for one xcb_image_t");

		c->px = xmalloc(w*h*4);

		c->x.image = xcb_image_create_native(c->conn, w, h,
				XCB_IMAGE_FORMAT_Z_PIXMAP, c->scr->root_depth, c->px,
				w*h*4, (uint8_t*)c->px);
	}
}

static void
__canvas_keep_visible(Canvas_t *c)
{
	if (c->pos.x < -c->width)
		c->pos.x = -c->width;

	if (c->pos.y < -c->height)
		c->pos.y = -c->height;

	if (c->pos.x > c->viewport_width)
		c->pos.x = c->viewport_width;

	if (c->pos.y > c->viewport_height)
		c->pos.y = c->viewport_height;

}

extern Canvas_t *
canvas_load(xcb_connection_t *conn, xcb_window_t win, const char *path)
{
	int x, y, w, h;
	xcb_screen_t *scr;
	unsigned char *px;
	Canvas_t *c;

	px = stbi_load(path, &w, &h, NULL, 4);

	if (NULL == px)
		return NULL;

	scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

	if (NULL == scr)
		die("can't get default screen");

	c = xcalloc(1, sizeof(Canvas_t));

	c->conn = conn;
	c->win = win;
	c->scr = scr;

	c->gc = xcb_generate_id(conn);
	c->shm = __x_check_mit_shm_extension(conn) ? 1 : 0;

	xcb_create_gc(conn, c->gc, win, 0, NULL);

	__canvas_set_size(c, w, h);

	for (y = 0; y < h; ++y)
		for (x = 0; x < w; ++x)
			c->px[y*w+x] = __pack_color(&px[(y*w+x)*4]);

	free(px);

	return c;
}

extern void
canvas_save(Canvas_t *c, const char *path)
{
	unsigned char *px;
	int x, y;

	px = xmalloc(c->width*c->height*4);

	for (y = 0; y < c->height; ++y)
		for (x = 0; x < c->width; ++x)
			__unpack_color(c->px[y*c->width+x], &px[(y*c->width+x)*4]);

	stbi_write_png(path, c->width, c->height, 4,
		px, c->width * 4);

	free(px);
}

extern void
canvas_crop(Canvas_t *c, int x, int y, int w, int h)
{
	int dy;
	uint32_t *crop_area;

	if (x < 0) w += x, x = 0;
	if (y < 0) h += y, y = 0;
	if (x + w >= c->width) w = c->width - x;
	if (y + h >= c->height) h = c->height - y;

	if (w < 1 || h < 1 || (w == c->width && h == c->height))
		return;

	crop_area = xmalloc(4*w*h);

	for (dy = 0; dy < h; ++dy) {
		memcpy(
			&crop_area[dy*w],
			&c->px[(y+dy)*c->width+x],
			4*w
		);
	}

	__canvas_set_size(c, w, h);
	canvas_move_relative(c, x, y);

	memcpy(c->px, crop_area, w*h*4);
	free(crop_area);
}

extern void
canvas_blur(Canvas_t *c, int x, int y, int w, int h, int strength)
{
	int dx, dy;
	int pass;
	int numpx, r, g, b, kdx, kdy;
	uint32_t *blur_area, *blur_area_previous, *tmp;

	if (x < 0) w += x, x = 0;
	if (y < 0) h += y, y = 0;
	if (x + w >= c->width) w = c->width - x;
	if (y + h >= c->height) h = c->height - y;

	if (w < 1 || h < 1)
		return;

	blur_area          = xmalloc(4*w*h);
	blur_area_previous = xmalloc(4*w*h);

	for (dy = 0; dy < h; ++dy) {
		memcpy(
			&blur_area[dy*w],
			&c->px[(y+dy)*c->width+x],
			4*w
		);
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
						r += RED(blur_area_previous[(dy+kdy)*w+dx+kdx]);
						g += GREEN(blur_area_previous[(dy+kdy)*w+dx+kdx]);
						b += BLUE(blur_area_previous[(dy+kdy)*w+dx+kdx]);
						numpx++;
					}
				}
				blur_area[dy*w+dx] = ((r/numpx)<<16) | ((g/numpx)<<8) | (b/numpx);
			}
		}
	}

	for (dy = 0; dy < h; ++dy) {
		memcpy(
			&c->px[(y+dy)*c->width+x],
			&blur_area[dy*w],
			4*w
		);
	}

	free(blur_area);
	free(blur_area_previous);
}

extern void
canvas_move_relative(Canvas_t *c, int offx, int offy)
{
	c->pos.x += offx;
	c->pos.y += offy;

	__canvas_keep_visible(c);
}

extern void
canvas_move_to_center(Canvas_t *c)
{
	c->pos.x = (c->viewport_width - c->width) / 2;
	c->pos.y = (c->viewport_height - c->height) / 2;
}

extern void
canvas_set_viewport(Canvas_t *c, int vw, int vh)
{
	c->viewport_width = vw;
	c->viewport_height = vh;

	__canvas_keep_visible(c);
}

extern void
canvas_render(Canvas_t *c)
{
	if (c->pos.y > 0)
		xcb_clear_area(c->conn, 0, c->win, 0, 0, c->viewport_width, c->pos.y);


	if (c->pos.y + c->height < c->viewport_height)
		xcb_clear_area(c->conn, 0, c->win, 0, c->pos.y + c->height,
				c->viewport_width, c->viewport_height - (c->pos.y + c->height));

	if (c->pos.x > 0)
		xcb_clear_area(c->conn, 0, c->win, 0, 0, c->pos.x, c->viewport_height);

	if (c->pos.x + c->width < c->viewport_width)
		xcb_clear_area(c->conn, 0, c->win, c->pos.x + c->width, 0,
				c->viewport_width - (c->pos.x + c->width), c->viewport_height);

	if (c->shm) {
		xcb_copy_area(c->conn, c->x.shm.pixmap, c->win,
				c->gc, 0, 0, c->pos.x, c->pos.y, c->width, c->height);
	} else {
		xcb_image_put(c->conn, c->win, c->gc,
				c->x.image, c->pos.x, c->pos.y, 0);
	}

	xcb_flush(c->conn);
}

extern void
canvas_viewport_to_canvas_pos(Canvas_t *c, int x, int y, int *out_x, int *out_y)
{
	*out_x = x - c->pos.x;
	*out_y = y - c->pos.y;
}

extern void
canvas_free(Canvas_t *c)
{
	xcb_free_gc(c->conn, c->gc);

	if (c->shm) {
		shmctl(c->x.shm.id, IPC_RMID, NULL);
		xcb_shm_detach(c->conn, c->x.shm.seg);
		shmdt(c->px);
		xcb_free_pixmap(c->conn, c->x.shm.pixmap);
	} else {
		xcb_image_destroy(c->x.image);
	}

	free(c);
}
