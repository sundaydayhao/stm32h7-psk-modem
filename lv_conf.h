#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

#define LV_MEM_CUSTOM 0
/*
 * Keep LVGL's pointer-heavy allocator in DTCM.  The external-SDRAM allocator
 * pool produced invalid 0xC018xxxx object pointers after repeated refreshes.
 * The large pixel buffers remain in external SDRAM.
 */
#define LV_MEM_ADR 0
#define LV_MEM_SIZE (24U * 1024U)
#define LV_MEM_POOL_INCLUDE "app/ui/lvgl_memory.h"
#define LV_MEM_POOL_ALLOC(size) lvgl_mem_pool_alloc(size)
#define LV_MEMCPY_MEMSET_STD 1

#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 30

/* display_tick_inc() advances LVGL from HAL_SYSTICK_Callback(). */
#define LV_TICK_CUSTOM 0

#define LV_DPI_DEF 130
#define LV_DRAW_COMPLEX 1
#define LV_USE_GPU_STM32_DMA2D 0

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0

#define LV_USE_LABEL 1
#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_CHART 0
#define LV_USE_ANIMIMG 0
#define LV_USE_CALENDAR 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN 0
#define LV_USE_LED 0
#define LV_USE_LIST 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 0
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0
#define LV_USE_IMG 1
#define LV_USE_LINE 0
#define LV_USE_ARC 0
#define LV_USE_TABLE 0
#define LV_USE_TEXTAREA 0
#define LV_USE_CANVAS 0
#define LV_USE_KEYBOARD 0
#define LV_USE_SPINNER 0

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 0
#define LV_FONT_MONTSERRAT_32 0
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_USE_FS_STDIO 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_WIN32 0
#define LV_USE_FS_FATFS 0

#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0
#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0

#endif
