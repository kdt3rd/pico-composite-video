#ifndef __CVIDEO_H__
#define __CVIDEO_H__

#include "hardware/pio.h"

#define CVIDEO_LINES 525
// Pixels per line should be a multiple of 32
// For very high or very low pixel counts, DATA_DELAY within cvideo.pio may need adjustment
#define CVIDEO_PIX_PER_LINE 640

typedef uint32_t *(*cvideo_data_callback_t)(uint32_t line);

void cvideo_init(cvideo_data_callback_t callback); 

#endif
