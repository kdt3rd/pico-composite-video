#ifndef __RENDERER_H__
#define __RENDERER_H__

#include <stdint.h>

void renderer_init(void);

void renderer_begin_drawing(void);

void renderer_end_drawing(void);

void renderer_draw_rect(unsigned int x, unsigned int y, unsigned int width, unsigned int height);

void renderer_draw_image(unsigned int x, unsigned int y, unsigned int width, unsigned int height, char *data);

#endif