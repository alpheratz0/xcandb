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

#include <png.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>
#include <xcb/xcb.h>
#include <xcb/xcb_cursor.h>
#include <xcb/xcb_image.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xproto.h>
#include <xcb/xkb.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#define UNUSED __attribute__((unused))

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

struct geometry {
	int x, y;
	int width, height;
};

static xcb_connection_t *conn;
static xcb_screen_t *screen;
static xcb_window_t window;
static xcb_gcontext_t gc;
static xcb_image_t *image;
static xcb_key_symbols_t *ksyms;
static xcb_cursor_context_t *cctx;
static xcb_cursor_t chand, carrow, ctcross;
static xcb_point_t dbp, dcp;
static xcb_point_t cbp, ccp;
static int start_in_fullscreen, cropping, dragging;
static int32_t wwidth, wheight, cwidth, cheight;
static uint32_t *wpx, *cpx;

static void
die(const char *fmt, ...)
{
	va_list args;

	fputs("xcandb: ", stderr);
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
	exit(1);
}

static const char *
enotnull(const char *str, const char *name)
{
	if (NULL == str)
		die("%s cannot be null", name);
	return str;
}

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
			              XCB_EVENT_MASK_KEY_RELEASE |
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
create_canvas(int32_t width, int32_t height)
{
	cwidth = width;
	cheight = height;
	cpx = malloc(sizeof(uint32_t) * cwidth * cheight);

	if (NULL == cpx)
		die("error while calling malloc, no memory available");

	memset(cpx, 255, sizeof(uint32_t) * cwidth * cheight);
}

static void
destroy_canvas(void)
{
	free(cpx);
}

static void
load_canvas(const char *path)
{
	FILE *fp;
	png_struct *png;
	png_info *pnginfo;
	png_byte **rows, bit_depth;
	int16_t x, y;

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
	create_canvas(png_get_image_width(png, pnginfo), png_get_image_height(png, pnginfo));

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

	rows = png_malloc(png, sizeof(png_byte *) * cheight);

	for (y = 0; y < cheight; ++y)
		rows[y] = png_malloc(png, png_get_rowbytes(png, pnginfo));

	png_read_image(png, rows);

	for (y = 0; y < cheight; ++y) {
		for (x = 0; x < cwidth; ++x) {
			if (rows[y][x*4+3] == 0)
				cpx[y*cwidth+x] = 0xffffff;
			else cpx[y*cwidth+x] = rows[y][x*4+0] << 16 |
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
}

static void
save_canvas(void)
{
	printf("saving canvas...\n");
}

static void
draw(void)
{
	int32_t x, y, ox, oy;

	memset(wpx, 0, sizeof(uint32_t) * wwidth * wheight);

	ox = (dcp.x - dbp.x) + (wwidth - cwidth) / 2;
	oy = (dcp.y - dbp.y) + (wheight - cheight) / 2;

	for (y = 0; y < cheight; ++y) {
		if ((y+oy) < 0 || (y+oy) >= wheight)
			continue;
		for (x = 0; x < cwidth; ++x) {
			if ((x+ox) < 0 || (x+ox) >= wwidth)
				continue;
			wpx[(y+oy)*wwidth+(x+ox)] = cpx[y*cwidth+x];
		}
	}

	for (x = MAX(MIN(cbp.x,ccp.x), 0); x < MIN(MAX(cbp.x,ccp.x), wwidth); ++x)
		if ((x%8)<6)
			wpx[cbp.y*wwidth+x] = wpx[ccp.y*wwidth+x] = 0xff;

	for (y = MAX(MIN(cbp.y,ccp.y), 0); y < MIN(MAX(cbp.y,ccp.y), wheight); ++y)
		if ((y%8)<6)
			wpx[y*wwidth+cbp.x] = wpx[y*wwidth+ccp.x] = 0xff;
}

static void
swap_buffers(void)
{
	xcb_image_put(conn, window, gc, image, 0, 0, 0);
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
crop_end(UNUSED int32_t x, UNUSED int32_t y)
{
	if (!cropping)
		return;

	int dx, dy;
	uint32_t *ncpx;
	struct geometry geom;

	cropping = 0;

	geom.x = MIN(cbp.x, ccp.x);
	geom.y = MIN(cbp.y, ccp.y);
	geom.width = MAX(cbp.x, ccp.x) - geom.x;
	geom.height = MAX(cbp.y, ccp.y) - geom.y;
	geom.x -= (dcp.x - dbp.x) + (wwidth - cwidth) / 2;
	geom.y -= (dcp.y - dbp.y) + (wheight - cheight) / 2;

	if (geom.x < 0) {
		geom.width += geom.x;
		geom.x = 0;
	}

	if (geom.y < 0) {
		geom.height += geom.y;
		geom.y = 0;
	}

	if (geom.x + geom.width >= cwidth)
		geom.width = cwidth - geom.x;

	if (geom.y + geom.height >= cheight)
		geom.height = cheight - geom.y;

	if (geom.width > 0 && geom.height > 0) {
		ncpx = malloc(sizeof(uint32_t)*geom.width*geom.height);

		for (dy = 0; dy < geom.height; ++dy)
			for (dx = 0; dx < geom.width; ++dx)
				ncpx[dy*geom.width+dx] = cpx[(geom.y+dy)*cwidth+geom.x+dx];

		free(cpx);
		cpx = ncpx;
		cwidth = geom.width;
		cheight = geom.height;

		dcp.x = dcp.y = dbp.x = dbp.y = 0;
	}

	ccp.x = ccp.y = cbp.x = cbp.y = 0;

	xcb_change_window_attributes(conn, window, XCB_CW_CURSOR, &carrow);
	draw();
	swap_buffers();
}

static void
crop_cancel(void)
{
	cropping = 0;
	ccp.x = ccp.y = cbp.x = cbp.y = 0;

	xcb_change_window_attributes(conn, window, XCB_CW_CURSOR, &carrow);
	draw();
	swap_buffers();
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
drag_end(UNUSED int32_t x, UNUSED int32_t y)
{
	dragging = 0;

	xcb_change_window_attributes(conn, window, XCB_CW_CURSOR, &carrow);
	xcb_flush(conn);
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
		destroy_canvas();
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
	xcb_keysym_t key;

	key = xcb_key_symbols_get_keysym(ksyms, ev->detail, 0);

	if (key == XKB_KEY_Escape)
		crop_cancel();
}

static void
h_key_release(xcb_key_release_event_t *ev)
{
	xcb_keysym_t key;

	key = xcb_key_symbols_get_keysym(ksyms, ev->detail, 0);

	if ((ev->state & XCB_MOD_MASK_CONTROL) && key == XKB_KEY_s)
		save_canvas();
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
	}
}

static void
h_motion_notify(xcb_motion_notify_event_t *ev)
{
	if (cropping) crop_update(ev->event_x, ev->event_y);
	if (dragging) drag_update(ev->event_x, ev->event_y);
}

static void
h_button_release(xcb_button_release_event_t *ev)
{
	switch (ev->detail) {
		case XCB_BUTTON_INDEX_1:
			crop_end(ev->event_x, ev->event_y);
			break;
		case XCB_BUTTON_INDEX_2:
			drag_end(ev->event_x, ev->event_y);
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

	load_canvas(loadpath);

	while ((ev = xcb_wait_for_event(conn))) {
		switch (ev->response_type & ~0x80) {
			case XCB_CLIENT_MESSAGE:     h_client_message((void *)(ev)); break;
			case XCB_EXPOSE:             h_expose((void *)(ev)); break;
			case XCB_KEY_PRESS:          h_key_press((void *)(ev)); break;
			case XCB_KEY_RELEASE:        h_key_release((void *)(ev)); break;
			case XCB_BUTTON_PRESS:       h_button_press((void *)(ev)); break;
			case XCB_MOTION_NOTIFY:      h_motion_notify((void *)(ev)); break;
			case XCB_BUTTON_RELEASE:     h_button_release((void *)(ev)); break;
			case XCB_CONFIGURE_NOTIFY:   h_configure_notify((void *)(ev)); break;
			case XCB_MAPPING_NOTIFY:     h_mapping_notify((void *)(ev)); break;
		}

		free(ev);
	}

	destroy_window();
	destroy_canvas();

	return 0;
}
