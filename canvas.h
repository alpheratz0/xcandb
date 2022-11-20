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
