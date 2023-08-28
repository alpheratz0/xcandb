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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_image.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xproto.h>
#include <xcb/xkb.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <saveas/saveas.h>
#include "util.h"
#include "canvas.h"

#define UNUSED __attribute__((unused))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

static xcb_connection_t *conn;
static xcb_screen_t *screen;
static xcb_window_t window;
static xcb_gcontext_t gc;
static xcb_image_t *image;
static xcb_key_symbols_t *ksyms;
static xcb_cursor_context_t *cctx;
static xcb_cursor_t chand, carrow, ctcross;
static xcb_point_t dbp, dcp, cbp, ccp, bbp, bcp;
static int start_in_fullscreen, dragging, cropping, blurring;
static int32_t wwidth, wheight;
static uint32_t *wpx;
struct canvas *canvas;

static xcb_atom_t
get_atom(const char *name)
{
	xcb_atom_t atom;
	xcb_generic_error_t *error;
	xcb_intern_atom_cookie_t cookie;
	xcb_intern_atom_reply_t *reply;

	cookie = xcb_intern_atom(conn, 0, strlen(name), name);
	reply = xcb_intern_atom_reply(conn, cookie, &error);

	if (NULL != error)
		die("xcb_intern_atom failed with error code: %hhu", error->error_code);

	atom = reply->atom;
	free(reply);

	return atom;
}

static void
create_window(void)
{
	if (xcb_connection_has_error(conn = xcb_connect(NULL, NULL)))
		die("can't open display");

	if (NULL == (screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data))
		die("can't get default screen");

	if (xcb_cursor_context_new(conn, screen, &cctx) != 0)
		die("can't create cursor context");

	chand = xcb_cursor_load_cursor(cctx, "fleur");
	carrow = xcb_cursor_load_cursor(cctx, "left_ptr");
	ctcross = xcb_cursor_load_cursor(cctx, "tcross");
	wwidth = 800, wheight = 600;

	if (NULL == (wpx = calloc(wwidth * wheight, sizeof(uint32_t))))
		die("error while calling malloc, no memory available");

	ksyms = xcb_key_symbols_alloc(conn);
	window = xcb_generate_id(conn);
	gc = xcb_generate_id(conn);

	xcb_create_window_aux(
		conn, screen->root_depth, window, screen->root, 0, 0,
		wwidth, wheight, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
		screen->root_visual, XCB_CW_EVENT_MASK,
		(const xcb_create_window_value_list_t []) {{
			.event_mask = XCB_EVENT_MASK_EXPOSURE |
			              XCB_EVENT_MASK_KEY_PRESS |
			              XCB_EVENT_MASK_BUTTON_PRESS |
			              XCB_EVENT_MASK_BUTTON_RELEASE |
			              XCB_EVENT_MASK_POINTER_MOTION |
			              XCB_EVENT_MASK_STRUCTURE_NOTIFY
		}}
	);

	xcb_create_gc(conn, gc, window, 0, NULL);

	image = xcb_image_create_native(
		conn, wwidth, wheight, XCB_IMAGE_FORMAT_Z_PIXMAP, screen->root_depth,
		wpx, sizeof(uint32_t) * wwidth * wheight, (uint8_t *)(wpx)
	);

	xcb_change_property(
		conn, XCB_PROP_MODE_REPLACE, window, get_atom("_NET_WM_NAME"),
		get_atom("UTF8_STRING"), 8, strlen("xcandb"), "xcandb"
	);

	xcb_change_property(
		conn, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS,
		XCB_ATOM_STRING, 8, strlen("xcandb\0xcandb\0"), "xcandb\0xcandb\0"
	);

	xcb_change_property(
		conn, XCB_PROP_MODE_REPLACE, window,
		get_atom("WM_PROTOCOLS"), XCB_ATOM_ATOM, 32, 1,
		(const xcb_atom_t []) { get_atom("WM_DELETE_WINDOW") }
	);

	xcb_change_property(
		conn, XCB_PROP_MODE_REPLACE, window,
		get_atom("_NET_WM_WINDOW_OPACITY"), XCB_ATOM_CARDINAL, 32, 1,
		(const uint8_t []) { 0xff, 0xff, 0xff, 0xff }
	);

	if (start_in_fullscreen) {
		xcb_change_property(
			conn, XCB_PROP_MODE_REPLACE, window,
			get_atom("_NET_WM_STATE"), XCB_ATOM_ATOM, 32, 1,
			(const xcb_atom_t []) { get_atom("_NET_WM_STATE_FULLSCREEN") }
		);
	}

	xcb_map_window(conn, window);

	xcb_xkb_use_extension(conn, XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION);

	xcb_xkb_per_client_flags(
		conn, XCB_XKB_ID_USE_CORE_KBD,
		XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 1, 0, 0, 0
	);

	xcb_flush(conn);
}

static void
destroy_window(void)
{
	xcb_free_gc(conn, gc);
	xcb_free_cursor(conn, chand);
	xcb_free_cursor(conn, carrow);
	xcb_free_cursor(conn, ctcross);
	xcb_key_symbols_free(ksyms);
	xcb_cursor_context_free(cctx);
	xcb_image_destroy(image);
	xcb_disconnect(conn);
}

static void
draw_dashed_rectangle(xcb_point_t a, xcb_point_t b)
{
	int x1, y1, x2, y2;
	int cx, cy;

	x1 = MAX(MIN(a.x, b.x), 0);
	y1 = MAX(MIN(a.y, b.y), 0);
	x2 = MIN(MAX(a.x, b.x), wwidth - 1);
	y2 = MIN(MAX(a.y, b.y), wheight - 1);

	for (cx = x1; cx < x2; ++cx)
		if ((cx % 8) < 6)
			wpx[y1*wwidth+cx] = wpx[y2*wwidth+cx] = 0xff;

	for (cy = y1; cy < y2; ++cy)
		if ((cy % 8) < 6)
			wpx[cy*wwidth+x1] = wpx[cy*wwidth+x2] = 0xff;
}

static void
draw(void)
{
	int32_t x, y, ox, oy;

	memset(wpx, 0, sizeof(uint32_t) * wwidth * wheight);

	ox = (dcp.x - dbp.x) + (wwidth - canvas->width) / 2;
	oy = (dcp.y - dbp.y) + (wheight - canvas->height) / 2;

	for (y = 0; y < canvas->height; ++y) {
		if ((y+oy) < 0 || (y+oy) >= wheight)
			continue;
		for (x = 0; x < canvas->width; ++x) {
			if ((x+ox) < 0 || (x+ox) >= wwidth)
				continue;
			wpx[(y+oy)*wwidth+(x+ox)] = canvas->pixels[y*canvas->width+x];
		}
	}

	if (cropping) {
		draw_dashed_rectangle(cbp, ccp);
	} else if (blurring) {
		draw_dashed_rectangle(bbp, bcp);
	}
}

static void
swap_buffers(void)
{
	xcb_image_put(conn, window, gc, image, 0, 0, 0);
	xcb_flush(conn);
}

static void
drag_begin(int16_t x, int16_t y)
{
	dragging = 1;

	/* keep previous offset */
	dbp.x = dbp.x - dcp.x + x;
	dbp.y = dbp.y - dcp.y + y;

	dcp.x = x;
	dcp.y = y;

	xcb_change_window_attributes(conn, window, XCB_CW_CURSOR, &chand);
	xcb_flush(conn);
}

static void
drag_update(int32_t x, int32_t y)
{
	dcp.x = x;
	dcp.y = y;

	draw();
	swap_buffers();
}

static void
drag_end(void)
{
	dragging = 0;

	xcb_change_window_attributes(conn, window, XCB_CW_CURSOR, &carrow);
	xcb_flush(conn);
}

static void
crop_begin(int16_t x, int16_t y)
{
	cropping = 1;

	cbp.x = ccp.x = x;
	cbp.y = ccp.y = y;

	xcb_change_window_attributes(conn, window, XCB_CW_CURSOR, &ctcross);
	xcb_flush(conn);
}

static void
crop_update(int32_t x, int32_t y)
{
	ccp.x = MIN(MAX(x, 0), wwidth);
	ccp.y = MIN(MAX(y, 0), wheight);

	draw();
	swap_buffers();
}

static void
crop_end(void)
{
	int x, y, width, height;

	if (!cropping)
		return;

	x = MIN(cbp.x, ccp.x);
	y = MIN(cbp.y, ccp.y);
	width = MAX(cbp.x, ccp.x) - x;
	height = MAX(cbp.y, ccp.y) - y;
	x -= (dcp.x - dbp.x) + (wwidth - canvas->width) / 2;
	y -= (dcp.y - dbp.y) + (wheight - canvas->height) / 2;

	canvas_crop(canvas, x, y, width, height);

	cropping = 0;
	dcp.x = dcp.y = dbp.x = dbp.y = 0;

	xcb_change_window_attributes(conn, window, XCB_CW_CURSOR, &carrow);
	draw();
	swap_buffers();
}

static void
blur_begin(int16_t x, int16_t y)
{
	blurring = 1;

	bbp.x = bcp.x = x;
	bbp.y = bcp.y = y;

	xcb_change_window_attributes(conn, window, XCB_CW_CURSOR, &ctcross);
	xcb_flush(conn);
}

static void
blur_update(int32_t x, int32_t y)
{
	bcp.x = MIN(MAX(x, 0), wwidth);
	bcp.y = MIN(MAX(y, 0), wheight);

	draw();
	swap_buffers();
}

static void
blur_end(void)
{
	int x, y, width, height;

	if (!blurring)
		return;

	x = MIN(bbp.x, bcp.x);
	y = MIN(bbp.y, bcp.y);
	width = MAX(bbp.x, bcp.x) - x;
	height = MAX(bbp.y, bcp.y) - y;
	x -= (dcp.x - dbp.x) + (wwidth - canvas->width) / 2;
	y -= (dcp.y - dbp.y) + (wheight - canvas->height) / 2;

	canvas_blur(canvas, x, y, width, height, 10);

	blurring = 0;

	xcb_change_window_attributes(conn, window, XCB_CW_CURSOR, &carrow);
	draw();
	swap_buffers();
}

static void
usage(void)
{
	puts("usage: xcandb [-fhv] [-l file]");
	exit(0);
}

static void
version(void)
{
	puts("xcandb version "VERSION);
	exit(0);
}

static void
h_client_message(xcb_client_message_event_t *ev)
{
	/* check if the wm sent a delete window message */
	/* https://www.x.org/docs/ICCCM/icccm.pdf */
	if (ev->data.data32[0] == get_atom("WM_DELETE_WINDOW")) {
		destroy_window();
		canvas_destroy(canvas);
		exit(0);
	}
}

static void
h_expose(UNUSED xcb_expose_event_t *ev)
{
	xcb_image_put(conn, window, gc, image, 0, 0, 0);
}

static void
h_key_press(xcb_key_press_event_t *ev)
{
	const char *savepath;
	xcb_keysym_t key;

	key = xcb_key_symbols_get_keysym(ksyms, ev->detail, 0);

	if ((ev->state & XCB_MOD_MASK_CONTROL) && key == XKB_KEY_s) {
		if (saveas_show_popup(&savepath) == SAVEAS_STATUS_OK)
			canvas_save(canvas, savepath);
	} else if (key == XKB_KEY_Escape) {
		cropping = blurring = 0;
		xcb_change_window_attributes(conn, window, XCB_CW_CURSOR, &carrow);
		draw();
		swap_buffers();
	}
}

static void
h_button_press(xcb_button_press_event_t *ev)
{
	switch (ev->detail) {
		case XCB_BUTTON_INDEX_1:
			crop_begin(ev->event_x, ev->event_y);
			break;
		case XCB_BUTTON_INDEX_2:
			drag_begin(ev->event_x, ev->event_y);
			break;
		case XCB_BUTTON_INDEX_3:
			blur_begin(ev->event_x, ev->event_y);
			break;
	}
}

static void
h_motion_notify(xcb_motion_notify_event_t *ev)
{
	if (dragging)
		drag_update(ev->event_x, ev->event_y);
	if (cropping)
		crop_update(ev->event_x, ev->event_y);
	if (blurring)
		blur_update(ev->event_x, ev->event_y);
}

static void
h_button_release(xcb_button_release_event_t *ev)
{
	switch (ev->detail) {
		case XCB_BUTTON_INDEX_1:
			crop_end();
			break;
		case XCB_BUTTON_INDEX_2:
			drag_end();
			break;
		case XCB_BUTTON_INDEX_3:
			blur_end();
			break;
	}
}

static void
h_configure_notify(xcb_configure_notify_event_t *ev)
{
	if (wwidth == ev->width && wheight == ev->height)
		return;

	/* this also destroys wpx */
	xcb_image_destroy(image);

	wwidth   =  ev->width;
	wheight  =  ev->height;
	wpx      =  malloc(wwidth * wheight * sizeof(uint32_t));

	image    =  xcb_image_create_native(conn, wwidth, wheight,
	                XCB_IMAGE_FORMAT_Z_PIXMAP, screen->root_depth, wpx,
	                sizeof(uint32_t) * wwidth * wheight, (uint8_t *)(wpx));

	draw();
	swap_buffers();
}

static void
h_mapping_notify(xcb_mapping_notify_event_t *ev)
{
	if (ev->count > 0)
		xcb_refresh_keyboard_mapping(ksyms, ev);
}

int
main(int argc, char **argv)
{
	const char *loadpath;
	xcb_generic_event_t *ev;

	loadpath = NULL;

	while (++argv, --argc > 0) {
		if ((*argv)[0] == '-' && (*argv)[1] != '\0' && (*argv)[2] == '\0') {
			switch ((*argv)[1]) {
				case 'h': usage(); break;
				case 'v': version(); break;
				case 'f': start_in_fullscreen = 1; break;
				case 'l': --argc; loadpath = enotnull(*++argv, "path"); break;
				default: die("invalid option %s", *argv); break;
			}
		} else {
			die("unexpected argument: %s", *argv);
		}
	}

	create_window();

	if (NULL == loadpath)
		die("a path should be specified");

	canvas = canvas_load(loadpath);

	while ((ev = xcb_wait_for_event(conn))) {
		switch (ev->response_type & ~0x80) {
			case XCB_CLIENT_MESSAGE:     h_client_message((void *)(ev)); break;
			case XCB_EXPOSE:             h_expose((void *)(ev)); break;
			case XCB_KEY_PRESS:          h_key_press((void *)(ev)); break;
			case XCB_BUTTON_PRESS:       h_button_press((void *)(ev)); break;
			case XCB_MOTION_NOTIFY:      h_motion_notify((void *)(ev)); break;
			case XCB_BUTTON_RELEASE:     h_button_release((void *)(ev)); break;
			case XCB_CONFIGURE_NOTIFY:   h_configure_notify((void *)(ev)); break;
			case XCB_MAPPING_NOTIFY:     h_mapping_notify((void *)(ev)); break;
		}

		free(ev);
	}

	destroy_window();
	canvas_destroy(canvas);

	return 0;
}
