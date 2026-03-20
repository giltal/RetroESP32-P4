#pragma once

/* LCD Resolution */
#define LCD_H_RES 480
#define LCD_V_RES 800

/* LCD Reset Pin */
#define LCD_RST   -1

/* LCD Backlight Pin */
#define LCD_BK_LIGHT_GPIO  23

/* Touch I2C Pins */
#define TP_I2C_SDA  7
#define TP_I2C_SCL  8
#define TP_RST      -1
#define TP_INT      -1

/* I2S / ES8311 Audio Codec Pins */
#define I2S_MCLK_IO   13
#define I2S_BCLK_IO   12
#define I2S_WS_IO     10
#define I2S_DOUT_IO    9   /* ESP32 -> ES8311 SDIN */
#define I2S_DIN_IO    48   /* ES8311 DOUT -> ESP32 */
#define AUDIO_PA_IO   11   /* Power Amplifier enable (active high) */

/* SD MMC Pins */
#define SD_MMC_CLK  43
#define SD_MMC_CMD  44
#define SD_MMC_D0   39
#define SD_MMC_D1   40
#define SD_MMC_D2   41
#define SD_MMC_D3   42
