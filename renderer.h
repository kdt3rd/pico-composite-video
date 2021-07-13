#ifndef __RENDERER_H__
#define __RENDERER_H__

#include <stdint.h>
#include <stdbool.h>

extern uint32_t renderer_screen_width;
extern uint32_t renderer_screen_height;

extern bool was_empty;

typedef enum {
  JUSTIFY_LEFT,
  JUSTIFY_RIGHT,
  JUSTIFY_CENTRE,
} renderer_text_justify_t;

void renderer_init(void);

void renderer_begin_drawing(void);

void renderer_end_drawing(void);

void renderer_draw_rect(unsigned int x, unsigned int y, unsigned int width, unsigned int height);

void renderer_draw_image(unsigned int x, unsigned int y, unsigned int width, unsigned int height, char *data);

void renderer_draw_character(unsigned int x, unsigned int y, unsigned int scale, char character);

void renderer_draw_string(unsigned int x, unsigned int y, unsigned int scale, char *text, unsigned int length, renderer_text_justify_t justification);

#endif