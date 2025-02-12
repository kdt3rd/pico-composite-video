#include "renderer.h"
#include "cvideo.h"
#include "connections.h"
#include <stdio.h>
#include <stdbool.h>
#include "font.xbm"

// 32 bits per word
#define LINE_WORD_COUNT CVIDEO_PIX_PER_LINE / 32
// Font spritesheet is a grid of 16x16
#define CHAR_WIDTH font_width / 16
#define CHAR_HEIGHT font_height / 16

uint32_t renderer_screen_width = CVIDEO_PIX_PER_LINE;
uint32_t renderer_screen_height = CVIDEO_LINES;

// Screen data buffers
typedef uint32_t buffer_t[CVIDEO_LINES][LINE_WORD_COUNT];
static buffer_t buffer_0;
static buffer_t buffer_1;
static buffer_t *output_buffer = &buffer_0;
static buffer_t *drawing_buffer = &buffer_1;

// Counters and flags for cvideo data callback
static volatile int current_line;
static volatile int current_pix;
static bool data_underrun;

// Callback and flags for signalling frame redraw
static renderer_draw_callback_t drawing_callback;
static bool drawing_in_progress;
static bool redraw_frame_requested;
static bool drawing_overrun;

static void set_bit(uint x, uint y, bool value);
static void renderer_clear(buffer_t *buffer);
static void update_output_buffer(void);

// Callback for shipping out image words to cvideo peripheral
uint32_t *data_callback(uint32_t line) {
    uint32_t *retline = (*output_buffer)[line];
    if (line == (CVIDEO_LINES - 1))
        update_output_buffer();
    return retline;
}

void renderer_init(renderer_draw_callback_t callback){
  renderer_clear(output_buffer);
  renderer_clear(drawing_buffer);
  data_underrun = false;
  drawing_in_progress = false;
  redraw_frame_requested = false;
  drawing_overrun = false;
  drawing_callback = callback;

  // PIO starts with odd lines
  current_line = 1;
  current_pix = 0;

  cvideo_init(data_callback);
}

void renderer_run(void) {
  if (data_underrun) {
    data_underrun = false;
    //printf("Data underrun!\r\n");
  }
  if (drawing_overrun) {
    //printf("Drawing too slow! \r\n");
    drawing_overrun = false;
  }

  if (redraw_frame_requested) {
    drawing_in_progress = true;
    renderer_clear(drawing_buffer);
    drawing_callback();
    drawing_in_progress = false;
    redraw_frame_requested = false;
  }
}

void renderer_draw_rect(uint x, uint y, uint width, uint height) {
  if (drawing_in_progress) {
    uint32_t x_word_start_index = x / 32;
    uint32_t x_word_end_index = (x + width) / 32;

    uint32_t x_word_start_boundary_offset = x % 32;
    uint32_t x_word_end_boundary_offset = (x + width) % 32;

    // Set first and last words manually
    // Set other words quickly

    for (int j = y; j < y + height; j++) {
      // Small rectangles sometimes fit entirely within one word
      if (x_word_start_index == x_word_end_index) {
        uint32_t data = 0xFFFFFFFF;
        data ^= 0xFFFFFFFF << x_word_start_boundary_offset;
        data ^= 0xFFFFFFFF >> (32 - x_word_end_boundary_offset);
        (*drawing_buffer)[j][x_word_start_index] |= data;
      }
      else {
        // First word and last word only partially filled
        (*drawing_buffer)[j][x_word_start_index] |= 0xFFFFFFFF << x_word_start_boundary_offset;
        (*drawing_buffer)[j][x_word_end_index] |= 0xFFFFFFFF >> (32 - x_word_end_boundary_offset);

        for (int i = x_word_start_index + 1; i < x_word_end_index; i++) {
          (*drawing_buffer)[j][i] = 0xFFFFFFFF;
        }
      }
    }
  }
}

void renderer_draw_image(unsigned int x, unsigned int y, unsigned int width, unsigned int height, char *data) {
  if (drawing_in_progress) {
    for (int j = 0; j < height; j++) {
      for  (int i = 0; i < width; i++) {
        // XBM images pad rows with zeros
        uint32_t array_position = (j * (width + (8 - width % 8))) + i;
        uint32_t array_index = array_position / 8;
        // XBM image, low bits are first
        uint32_t byte_position = array_position % 8;

        uint8_t value = data[array_index] >> byte_position & 1 ;
        // XBM images use 1 for black, so invert values
        set_bit(x + i, y + j, !value);
      }
    }
  }
}

void renderer_draw_character(unsigned int x, unsigned int y, unsigned int scale, char character) {
  if (drawing_in_progress) {
    uint8_t row = character / 16;
    uint8_t column = character % 16;
    uint32_t font_x = column * 10;
    uint32_t font_y = row * 12;

    for (int j = font_y; j < font_y + CHAR_HEIGHT; j++) {
      for  (int i = font_x; i <  font_x + CHAR_WIDTH; i++) {
        // XBM images pad rows with zeros
        uint32_t array_position;
        if (font_width %8 != 0) {
          array_position = (j * (font_width + (8 - font_width % 8))) + i;
        }
        else {
          array_position = (j * font_width) + i;
        }
        uint32_t array_index = array_position / 8;
        // XBM image, low bits are first
        uint32_t byte_position = array_position % 8;

        uint8_t value = font_bits[array_index] >> byte_position & 1 ;

        for (int sx = 0; sx < scale; sx++) {
          for (int sy = 0; sy < scale; sy++) {
            set_bit(x + ((i - font_x) * scale) + sx, y + ((j- font_y) * scale) + sy, !value);
          }
        }
      }
    }
  }
}

void renderer_draw_string(unsigned int x, unsigned int y, unsigned int scale, char *text, unsigned int length, renderer_text_justify_t justification){
  if (drawing_in_progress) {
    switch (justification) {
      case JUSTIFY_LEFT:
        for(int i = 0; i < length; i++) {
          renderer_draw_character(x + (i * CHAR_WIDTH * scale), y, scale, text[i]);
        }
        break;

      case JUSTIFY_CENTRE:
        for(int i = 0; i < length; i++) {
          renderer_draw_character(x - (length * scale * CHAR_WIDTH / 2) + (i * CHAR_WIDTH * scale), y, scale, text[i]);
        }
        break;

      case JUSTIFY_RIGHT:
        for (int i = 0; i < length; i++) {
          renderer_draw_character(x - ((length - i) * CHAR_WIDTH * scale), y, scale, text[i]);
        }
        break;

      default:
          //printf("Invalid justification\r\n");
    }
  }
}

static void update_output_buffer(void){
  if(!drawing_in_progress){
    // Swap drawing and output buffer
    buffer_t *temp = output_buffer;
    output_buffer = drawing_buffer;
    drawing_buffer = temp;
  }
  if (redraw_frame_requested) {
    drawing_overrun = true;
  }
  redraw_frame_requested = true;
}

static void renderer_clear(buffer_t *buffer) {
  for (int i =0; i < CVIDEO_LINES; i++) {
    for (int j = 0; j < CVIDEO_PIX_PER_LINE / 32; j++) {
      (*buffer)[i][j] = 0;
    }
  }
}

static void set_bit(uint x, uint y, bool value) {
  uint index_x = x / 32;
  uint pos_x = x % 32;

  uint flag = value << pos_x;
  (*drawing_buffer)[y][index_x] |= flag;
}
