#ifndef _BOARDS_SLOW_LFO_RP2350_MATRIX_H
#define _BOARDS_SLOW_LFO_RP2350_MATRIX_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)
#define PICO_RP2350A 1

// Waveshare RP2350-Matrix: 16 MB external flash and 8x8 WS2812 on GP25.
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (16 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

#ifndef PICO_DEFAULT_WS2812_PIN
#define PICO_DEFAULT_WS2812_PIN 25
#endif

#endif
