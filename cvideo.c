#include "connections.h"
#include "cvideo.h"
#include "cvideo.pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include <stdio.h>

// Sync PIO needs 2us per instruction
//#define SYNC_INTERVAL 0.000002
#define SYNC_INTERVAL ((float)(27*15734))
// Data transmits for 52.6us
#define DATA_INTERVAL 0.0000526

#define DATA_SM_ID 0
#define SYNC_SM_ID 1

static PIO cvideo_pio;
static uint data_sync_irq_id = PIO0_IRQ_0;
static uint data_stream_irq_id = DMA_IRQ_0;
static cvideo_data_callback_t data_callback;
static uint dma_data_channel;
static uint data_vline = 0;
#define WIDTH_UINT32 ((CVIDEO_PIX_PER_LINE + 31) / 32)
static uint32_t zero_buf[WIDTH_UINT32] = {0};

//uint dma_channel_0;

static inline void cvsync_program_init(PIO pio, uint sm, uint offset, float clockdiv, uint sync_pin);
static inline void cvdata_program_init(PIO pio, uint sm, uint offset, float clockdiv, uint data_pin);

//static void cvideo_sync_dma_handler(void)
//{
//    // Switch condition on the vertical scanline number (vline)
//    // Each statement does a dma_channel_set_read_addr to point the PIO to the next data to output
//    //
//    switch(vline)
//    {
//        // First deal with the vertical sync scanlines
//        // Also on scanline 3, preload the first pixel buffer scanline
//        //
//        case 1 ... 2:
//            dma_channel_set_read_addr(dma_channel_0, vsync_ll, true);
//            break;
//        case 3:
//            dma_channel_set_read_addr(dma_channel_0, vsync_ls, true);
//            break;
//        case 4 ... 5:
//        case 310 ... 312:
//            dma_channel_set_read_addr(dma_channel_0, vsync_ss, true);
//            break;
//
//        // Then the border scanlines
//        //
//        case 6 ... 68:
//        case 261 ... 309:
//            dma_channel_set_read_addr(dma_channel_0, border, true);
//            break;
//
//        // Now point the dma at the first buffer for the pixel data,
//        // and preload the data for the next scanline
//        //
//        default:
//            dma_channel_set_read_addr(dma_channel_0, hsync, true);
//            break;
//    }
//
//    // Increment and wrap the counters
//    //
//    if(vline++ >= 312) {    // If we've gone past the bottom scanline then
//        vline = 1;		    // Reset the scanline counter
//        vblank_count++;
//    }
//
//    // Finally, clear the interrupt request ready for the next horizontal sync interrupt
//    //
//    dma_hw->ints0 = 1u << dma_channel_0;	
//}

static void cvideo_configure_pio_dma(
    PIO pio, uint sm, uint dma_channel, uint transfer_size,
    size_t buffer_size)//, irq_handler_t handler)
{
    dma_channel_config c = dma_channel_get_default_config(dma_channel);

    channel_config_set_transfer_data_size(&c, transfer_size);
    channel_config_set_read_increment(&c, true);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));

    dma_channel_configure(
        dma_channel, &c, &pio->txf[sm], NULL, buffer_size, true /* start immediately */);

    //if(handler != NULL)
    //{
    //    dma_channel_set_irq0_enabled(dma_channel, true);
    //    irq_set_exclusive_handler(DMA_IRQ_0, handler);
    //    irq_set_enabled(DMA_IRQ_0, true);
    //}
}

static void cvideo_data_pio_handler_ntsc(void)
{
    if(data_vline >= CVIDEO_LINES) {
        data_vline = 0;
    }
    switch (data_vline)
    {
        case 0 ... 19:
        case 260 ... 261:
        case 262 ... 281:
        case 522 ... 524:
        default:
            dma_channel_set_read_addr(dma_data_channel, zero_buf, true);
            data_vline++;
            break;
        case 20 ... 259:
        case 282 ... 521:
            dma_channel_set_read_addr(dma_data_channel, data_callback(data_vline++), true);
            break;
    }

    // reset IRQ
    irq_clear(data_stream_irq_id);
    //hw_set_bits(&cvideo_pio->irq, 1u);
}

static void cvideo_data_pio_handler_other(void)
{
    if(data_vline >= CVIDEO_LINES) {
        data_vline = 0;
    }
    dma_channel_set_read_addr(dma_data_channel, data_callback(data_vline++), true);

    // reset IRQ
    irq_clear(data_stream_irq_id);
    //hw_set_bits(&cvideo_pio->irq, 1u);
}

//static void cvdata_isr(void)
//{
//    pio_sm_put(cvideo_pio, DATA_SM_ID, data_callback());
//    irq_clear(pio_irq_id);
//}

void cvideo_init(cvideo_data_callback_t callback) {
    uint data_pin = CVIDEO_DATA_PIN;
    uint sync_pin = CVIDEO_SYNC_PIN;
    data_callback = callback;

    cvideo_pio = pio0;
    data_sync_irq_id = PIO0_IRQ_0;
    //cvideo_pio = pio1;
    //data_sync_irq_id = PIO1_IRQ_0;

    dma_data_channel = dma_claim_unused_channel(true);
    float data_clockdiv = (clock_get_hz(clk_sys) * DATA_INTERVAL) / ((float)(CVIDEO_PIX_PER_LINE * CLOCKS_PER_BIT));
    float sync_clockdiv = clock_get_hz(clk_sys) / SYNC_INTERVAL;

    pio_sm_set_enabled(cvideo_pio, SYNC_SM_ID, false); // Disable the PIO state machine
    pio_sm_clear_fifos(cvideo_pio, SYNC_SM_ID); // Clear the PIO FIFO buffers
    pio_sm_set_enabled(cvideo_pio, DATA_SM_ID, false); // Disable the PIO state machine
    pio_sm_clear_fifos(cvideo_pio, DATA_SM_ID); // Clear the PIO FIFO buffers

    uint offset_sync = pio_add_program(cvideo_pio, &cvsync_program);
    uint offset_data = pio_add_program(cvideo_pio, &cvdata_program);

    cvdata_program_init(cvideo_pio, DATA_SM_ID, offset_data, data_clockdiv, data_pin);
    cvsync_program_init(cvideo_pio, SYNC_SM_ID, offset_sync, sync_clockdiv, sync_pin);

    cvideo_configure_pio_dma( cvideo_pio, DATA_SM_ID, dma_data_channel, DMA_SIZE_32, WIDTH_UINT32 );

    if (CVIDEO_LINES == 525)
        irq_set_exclusive_handler(data_stream_irq_id, cvideo_data_pio_handler_ntsc);
    else
        irq_set_exclusive_handler(data_stream_irq_id, cvideo_data_pio_handler_other);
    irq_set_enabled(data_stream_irq_id, true);
    //irq_set_priority(pio_irq_id, 0);
    cvideo_pio->inte0 = PIO_IRQ0_INTE_SM0_BITS;
    irq_set_enabled(data_sync_irq_id, true);

    if (CVIDEO_PIX_PER_LINE % 32 != 0) {
        //printf("ERROR: Horizontal pixel count must be a multiple of 32\r\n");
    }

    // Set the state machines running
	pio_enable_sm_mask_in_sync(cvideo_pio, (1u << DATA_SM_ID) | (1u << SYNC_SM_ID));
    //pio_sm_set_enabled(pio, sm, true);
}

static inline void cvdata_program_init(PIO pio, uint sm, uint offset, float clockdiv, uint data_pin) {
    pio_sm_config c = cvdata_program_get_default_config(offset);

    pio_gpio_init(pio, data_pin);
    pio_sm_set_consecutive_pindirs(pio, sm, data_pin, 1, true);

    // Map the state machine's OUT and SIDE pin group to one pin, namely the `pin`
    // parameter to this function.
    sm_config_set_set_pins(&c, data_pin, 1);
    sm_config_set_out_pins(&c, data_pin, 1);

    sm_config_set_clkdiv(&c, clockdiv);

    // Load our configuration, and jump to the start of the program
    pio_sm_init(pio, sm, offset, &c);

    // Tell the state machine the number of pixels per line (minus 1)
    pio_sm_put(pio, sm, CVIDEO_PIX_PER_LINE - 1);
    pio_sm_exec(pio, sm, 0x80a0);  // pull
    pio_sm_exec(pio, sm, 0xa027);  // mov    x, osr 
    pio_sm_exec(pio, sm, 0x6060);  // out null 32 ; Discard OSR contents after copying to x
}

static inline void cvsync_program_init(PIO pio, uint sm, uint offset, float clockdiv, uint sync_pin) {
    pio_sm_config c = cvsync_program_get_default_config(offset);

    pio_gpio_init(pio, sync_pin);
    pio_sm_set_consecutive_pindirs(pio, sm, sync_pin, 1, true);

    // Map the state machine's OUT and SIDE pin group to one pin, namely the `pin`
    // parameter to this function.
    sm_config_set_sideset_pins(&c, sync_pin);

    // Set the clock speed
    sm_config_set_clkdiv(&c, clockdiv);

    //sm_config_set_out_shift(&c, false, true, 8);

    pio_sm_init(pio, sm, offset, &c);

    // Tell the state machine the number of video lines per frame (minus 1)
    pio_sm_put(pio, sm, (CVIDEO_LINES / 2) - 1);
    pio_sm_exec(pio, sm, 0x80a0);  // pull side 0
}
