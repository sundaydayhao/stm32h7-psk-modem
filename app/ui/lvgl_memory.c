#include "lvgl_memory.h"

#include <stdint.h>

#include "lv_conf.h"

/* LVGL calls this once from lv_mem_init().  DTCM is CPU-local and avoids the
 * external SDRAM pointer alias that corrupts LVGL's object tree. */
static uint8_t s_lvgl_memory[LV_MEM_SIZE]
    __attribute__((section(".dtcm_bss"), aligned(8)));

void *lvgl_mem_pool_alloc(size_t size)
{
    return (size <= sizeof(s_lvgl_memory)) ? s_lvgl_memory : NULL;
}
