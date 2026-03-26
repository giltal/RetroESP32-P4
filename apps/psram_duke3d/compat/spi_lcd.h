/*
 * spi_lcd.h compat header for Duke3D PSRAM app.
 * The engine's display.c calls spi_lcd_send_boarder() via SDL_Flip.
 * We provide this in papp_sdl_video.c.
 */
#ifndef SPI_LCD_H
#define SPI_LCD_H

#include <stdint.h>

extern int16_t lcdpal[256];

void spi_lcd_send(uint16_t *scr);
void spi_lcd_send_boarder(void *pixels, int border);
void spi_lcd_init(void);

#endif
