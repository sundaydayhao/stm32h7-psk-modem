cmake_minimum_required(VERSION 3.22)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(stm32h743iit6_psk_modem C ASM)

add_executable(${PROJECT_NAME})

target_compile_definitions(${PROJECT_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32H743xx
    $<$<CONFIG:Debug>:DEBUG>
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall
    -Wextra
    -Wno-unused-parameter
    -ffunction-sections
    -fdata-sections
    -fno-common
)

target_include_directories(${PROJECT_NAME} PRIVATE
    Core/Inc
    app
    app/dsp
    app/modem
    app/board
    app/platform
    app/ui
    third_party/psk_rtty/include
    Drivers/STM32H7xx_HAL_Driver/Inc
    Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32H7xx/Include
    Drivers/CMSIS/Include
)

set(HAL_SRC
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_cortex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_exti.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_flash_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_hsem.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2s.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2s_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_ltdc.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_ltdc_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_mdma.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_sdram.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_spi_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart_ex.c
    Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_ll_fmc.c
)

target_sources(${PROJECT_NAME} PRIVATE
    startup_stm32h743xx.s
    Core/Src/main.c
    Core/Src/stm32h7xx_it.c
    Core/Src/system_stm32h7xx.c
    Core/Src/syscalls.c
    Core/Src/sysmem.c
    app/dsp/spectrum.c
    app/board/fire_lcd.c
    app/modem/psk_modes.c
    app/modem/psk_modem.c
    app/platform/audio_decim.c
    app/platform/audio_input.c
    app/platform/touch_gt911.c
    app/ui/lvgl_memory.c
    app/ui/display.c
    third_party/psk_rtty/psk_rtty.c
    ${HAL_SRC}
)

set(LV_CONF_PATH "${CMAKE_SOURCE_DIR}/lv_conf.h" CACHE STRING "LVGL configuration header" FORCE)
set(LV_LVGL_H_INCLUDE_SIMPLE ON CACHE BOOL "Use a simple lvgl.h include" FORCE)
set(LV_CONF_INCLUDE_SIMPLE ON CACHE BOOL "Use a simple lv_conf.h include" FORCE)
add_subdirectory(third_party/lvgl EXCLUDE_FROM_ALL)

set_source_files_properties(
    app/dsp/spectrum.c
    third_party/psk_rtty/psk_rtty.c
    PROPERTIES COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>"
)

target_link_options(${PROJECT_NAME} PRIVATE
    -T${CMAKE_SOURCE_DIR}/STM32H743IITX_FLASH.ld
    -Wl,-Map=${PROJECT_NAME}.map
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nano.specs
)

target_link_libraries(${PROJECT_NAME} PRIVATE m lvgl)

get_filename_component(TOOLCHAIN_BIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
find_program(ARM_SIZE_TOOL
    NAMES ${CMAKE_SIZE} arm-none-eabi-size starm-size
    HINTS "${TOOLCHAIN_BIN_DIR}"
)

if(ARM_SIZE_TOOL)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${ARM_SIZE_TOOL} $<TARGET_FILE:${PROJECT_NAME}>
    )
endif()
