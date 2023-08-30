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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_cursor.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <xcb/xkb.h>

#include "utils.h"
#include "canvas.h"
#include "log.h"

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

typedef struct {
	bool active;
	int x;
	int y;
} DragInfo_t;

typedef struct {
	bool active;
	xcb_point_t start;
	xcb_point_t end;
} CropInfo_t;

typedef struct {
	bool active;
	xcb_point_t start;
	xcb_point_t end;
} BlurInfo_t;

static Canvas_t *canvas;
static xcb_connection_t *conn;
static xcb_screen_t *scr;
static xcb_window_t win;
static xcb_gcontext_t rect_gc;
static xcb_key_symbols_t *ksyms;
static xcb_cursor_context_t *cctx;
static xcb_cursor_t cursor_hand;
static xcb_cursor_t cursor_arrow;
static xcb_cursor_t cursor_crosshair;
static DragInfo_t drag;
static CropInfo_t crop;
static BlurInfo_t blur;
static bool start_in_fullscreen;
static bool should_close;

static xcb_atom_t
get_x11_atom(const char *name)
{
	xcb_atom_t atom;
	xcb_generic_error_t *error;
	xcb_intern_atom_cookie_t cookie;
	xcb_intern_atom_reply_t *reply;

	cookie = xcb_intern_atom(conn, 0, strlen(name), name);
	reply = xcb_intern_atom_reply(conn, cookie, &error);

	if (NULL != error)
		die("xcb_intern_atom failed with error code: %hhu",
				error->error_code);

	atom = reply->atom;
	free(reply);

	return atom;
}

static void
xwininit(void)
{
	const char *wm_class,
		       *wm_name;

	xcb_atom_t _NET_WM_NAME,
			   _NET_WM_WINDOW_OPACITY;

	xcb_atom_t WM_PROTOCOLS,
			   WM_DELETE_WINDOW;

	xcb_atom_t _NET_WM_STATE,
			   _NET_WM_STATE_FULLSCREEN;

	xcb_atom_t UTF8_STRING;

	uint8_t opacity[4];

	conn = xcb_connect(NULL, NULL);

	if (xcb_connection_has_error(conn))
		die("can't open display");

	scr = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;

	if (NULL == scr)
		die("can't get default screen");

	if (xcb_cursor_context_new(conn, scr, &cctx) != 0)
		die("can't create cursor context");

	cursor_hand = xcb_cursor_load_cursor(cctx, "fleur");
	cursor_arrow = xcb_cursor_load_cursor(cctx, "left_ptr");
	cursor_crosshair = xcb_cursor_load_cursor(cctx, "crosshair");
	ksyms = xcb_key_symbols_alloc(conn);
	win = xcb_generate_id(conn);
	rect_gc = xcb_generate_id(conn);

	xcb_create_window_aux(
		conn, scr->root_depth, win, scr->root, 0, 0,
		800, 600, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
		scr->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
		(const xcb_create_window_value_list_t []) {{
			.background_pixel = 0x1e1e1e,
			.event_mask = XCB_EVENT_MASK_EXPOSURE |
			              XCB_EVENT_MASK_KEY_PRESS |
			              XCB_EVENT_MASK_BUTTON_PRESS |
			              XCB_EVENT_MASK_BUTTON_RELEASE |
			              XCB_EVENT_MASK_POINTER_MOTION |
			              XCB_EVENT_MASK_STRUCTURE_NOTIFY
		}}
	);

	_NET_WM_NAME = get_x11_atom("_NET_WM_NAME");
	UTF8_STRING = get_x11_atom("UTF8_STRING");
	wm_name = "xcandb";

	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win,
		_NET_WM_NAME, UTF8_STRING, 8, strlen(wm_name), wm_name);

	wm_class = "xcandb\0xcandb\0";
	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win, XCB_ATOM_WM_CLASS,
		XCB_ATOM_STRING, 8, strlen(wm_class), wm_class);

	WM_PROTOCOLS = get_x11_atom("WM_PROTOCOLS");
	WM_DELETE_WINDOW = get_x11_atom("WM_DELETE_WINDOW");

	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win,
		WM_PROTOCOLS, XCB_ATOM_ATOM, 32, 1, &WM_DELETE_WINDOW);

	_NET_WM_WINDOW_OPACITY = get_x11_atom("_NET_WM_WINDOW_OPACITY");
	opacity[0] = opacity[1] = opacity[2] = opacity[3] = 0xff;

	xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win,
		_NET_WM_WINDOW_OPACITY, XCB_ATOM_CARDINAL, 32, 1, opacity);

	_NET_WM_STATE = get_x11_atom("_NET_WM_STATE");
	_NET_WM_STATE_FULLSCREEN = get_x11_atom("_NET_WM_STATE_FULLSCREEN");

	if (start_in_fullscreen) {
		xcb_change_property(conn, XCB_PROP_MODE_REPLACE, win,
			_NET_WM_STATE, XCB_ATOM_ATOM, 32, 1, &_NET_WM_STATE_FULLSCREEN);
	}

	xcb_create_gc(conn, rect_gc, win, XCB_GC_FOREGROUND | XCB_GC_LINE_STYLE,
			(const uint32_t []) { 0xffffff, XCB_LINE_STYLE_DOUBLE_DASH });

	xcb_xkb_use_extension(conn, XCB_XKB_MAJOR_VERSION, XCB_XKB_MINOR_VERSION);

	xcb_xkb_per_client_flags(
		conn, XCB_XKB_ID_USE_CORE_KBD,
		XCB_XKB_PER_CLIENT_FLAG_DETECTABLE_AUTO_REPEAT, 1, 0, 0, 0
	);

	xcb_change_window_attributes(conn, win, XCB_CW_CURSOR, &cursor_arrow);
	xcb_map_window(conn, win);
	xcb_flush(conn);
}

static void
xwindestroy(void)
{
	xcb_free_gc(conn, rect_gc);
	xcb_free_cursor(conn, cursor_hand);
	xcb_free_cursor(conn, cursor_arrow);
	xcb_free_cursor(conn, cursor_crosshair);
	xcb_key_symbols_free(ksyms);
	xcb_destroy_window(conn, win);
	xcb_cursor_context_free(cctx);
	xcb_disconnect(conn);
}

static void
save(void)
{
	char *path, *expanded_path;

	if (NULL == (path = xprompt("save as...")))
		return;

	if (NULL == (expanded_path = path_expand(path))) {
		info("could not expand path");
	} else if (path_is_writeable(expanded_path)) {
		canvas_save(canvas, expanded_path);
		info("saved image succesfully to %s", path);
	} else {
		info("can't save to %s", path);
	}

	free(path);
	free(expanded_path);
}

static xcb_rectangle_t
rect_from_two_points(xcb_point_t p1, xcb_point_t p2)
{
	int x, y;

	x = MIN(p1.x, p2.x);
	y = MIN(p1.y, p2.y);

	return (xcb_rectangle_t) {
		.x      =                   x,
		.y      =                   y,
		.width  = MAX(p1.x, p2.x) - x,
		.height = MAX(p1.y, p2.y) - y
	};
}

static void
draw_dashed_rectangle(xcb_point_t a, xcb_point_t b)
{
	xcb_poly_rectangle(conn, win, rect_gc, 1,
			(const xcb_rectangle_t []) { rect_from_two_points(a, b)});
}

static void
drag_begin(int16_t x, int16_t y)
{
	if (crop.active || blur.active)
		return;
	drag.active = true;
	drag.x = x;
	drag.y = y;
	xcb_change_window_attributes(conn, win, XCB_CW_CURSOR, &cursor_hand);
	xcb_flush(conn);
}

static void
drag_update(int16_t x, int16_t y)
{
	int dx, dy;

	if (!drag.active)
		return;

	dx = drag.x - x;
	dy = drag.y - y;

	drag.x = x;
	drag.y = y;

	canvas_camera_move_relative(canvas, dx, dy);
	canvas_render(canvas);
}

static void
drag_end(void)
{
	if (!drag.active)
		return;

	drag.active = false;
	xcb_change_window_attributes(conn, win, XCB_CW_CURSOR, &cursor_arrow);
	xcb_flush(conn);
}

static void
crop_begin(int16_t x, int16_t y)
{
	if (drag.active || blur.active)
		return;

	crop.active = true;

	crop.start.x = crop.end.x = x;
	crop.start.y = crop.end.y = y;

	xcb_change_window_attributes(conn, win, XCB_CW_CURSOR, &cursor_crosshair);
	xcb_flush(conn);
}

static void
crop_update(int16_t x, int16_t y)
{
	if (!crop.active)
		return;

	crop.end.x = x;
	crop.end.y = y;

	canvas_render(canvas);
	draw_dashed_rectangle(crop.start, crop.end);
	xcb_flush(conn);
}

static void
crop_end(void)
{
	xcb_rectangle_t crop_rect;
	int x, y;

	if (!crop.active)
		return;

	crop.active = false;
	crop_rect = rect_from_two_points(crop.start, crop.end);

	canvas_camera_to_canvas_pos(canvas, crop_rect.x, crop_rect.y, &x, &y);
	canvas_crop(canvas, x, y, crop_rect.width, crop_rect.height);
	canvas_camera_to_center(canvas);
	canvas_render(canvas);

	xcb_change_window_attributes(conn, win, XCB_CW_CURSOR, &cursor_arrow);
}

static void
blur_begin(int16_t x, int16_t y)
{
	if (drag.active || crop.active)
		return;

	blur.active = true;

	blur.start.x = blur.end.x = x;
	blur.start.y = blur.end.y = y;

	xcb_change_window_attributes(conn, win, XCB_CW_CURSOR, &cursor_crosshair);
	xcb_flush(conn);
}

static void
blur_update(int16_t x, int16_t y)
{
	if (!blur.active)
		return;

	blur.end.x = x;
	blur.end.y = y;

	canvas_render(canvas);
	draw_dashed_rectangle(blur.start, blur.end);
	xcb_flush(conn);
}

static void
blur_end(void)
{
	xcb_rectangle_t blur_rect;
	int x, y;

	if (!blur.active)
		return;

	blur.active = false;
	blur_rect = rect_from_two_points(blur.start, blur.end);

	canvas_camera_to_canvas_pos(canvas, blur_rect.x, blur_rect.y, &x, &y);
	canvas_blur(canvas, x, y, blur_rect.width, blur_rect.height, 10);
	canvas_render(canvas);

	xcb_change_window_attributes(conn, win, XCB_CW_CURSOR, &cursor_arrow);
}

static void
h_client_message(xcb_client_message_event_t *ev)
{
	xcb_atom_t WM_DELETE_WINDOW;

	WM_DELETE_WINDOW = get_x11_atom("WM_DELETE_WINDOW");

	/* check if the wm sent a delete window message */
	/* https://www.x.org/docs/ICCCM/icccm.pdf */
	if (ev->data.data32[0] == WM_DELETE_WINDOW)
		should_close = true;
}

static void
h_expose(xcb_expose_event_t *ev)
{
	(void) ev;
	canvas_render(canvas);
}

static void
h_key_press(xcb_key_press_event_t *ev)
{
	xcb_keysym_t key;

	key = xcb_key_symbols_get_keysym(ksyms, ev->detail, 0);

	if (ev->state & XCB_MOD_MASK_CONTROL) {
		switch (key) {
		case XKB_KEY_s: save(); return;
		}
	}

	switch (key) {
	case XKB_KEY_Escape:
		crop.active = blur.active = false;
		xcb_change_window_attributes(conn, win, XCB_CW_CURSOR, &cursor_arrow);
		canvas_render(canvas);
		break;
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
	if (drag.active)
		drag_update(ev->event_x, ev->event_y);
	if (crop.active)
		crop_update(ev->event_x, ev->event_y);
	if (blur.active)
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
	canvas_set_viewport(canvas, ev->width, ev->height);
	canvas_camera_to_center(canvas);
}

static void
h_mapping_notify(xcb_mapping_notify_event_t *ev)
{
	if (ev->count > 0)
		xcb_refresh_keyboard_mapping(ksyms, ev);
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
			case 'f': start_in_fullscreen = true; break;
			case 'l': --argc; loadpath = enotnull(*++argv, "path"); break;
			default: die("invalid option %s", *argv); break;
			}
		} else {
			die("unexpected argument: %s", *argv);
		}
	}

	xwininit();

	if (NULL == loadpath)
		die("a path should be specified");

	canvas = canvas_load(conn, win, loadpath);

	if (NULL == canvas)
		die("could not load the specified image");

	while (!should_close && (ev = xcb_wait_for_event(conn))) {
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

	canvas_free(canvas);
	xwindestroy();

	return 0;
}
