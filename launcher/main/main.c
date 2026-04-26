  #include "includes/core.h"
  #include "includes/definitions.h"
  #include "includes/structures.h"
  #include "includes/declarations.h"
//}#pragma endregion Includes

//{#pragma region Odroid
  static odroid_gamepad_state gamepad;
  odroid_battery_state battery_state;
//}#pragma endregion Odroid


//{#pragma region Global
  bool SAVED = false;
  bool RESTART = false;
  bool SAFE_MODE = false;
  bool LAUNCHER = false;
  bool FOLDER = false;
  bool SPLASH = true;
  bool SETTINGS = false;
  bool BROWSER = false;

  #define BROWSER_LIMIT 10
  int BROWSER_SEL = 0; /* cursor position within visible page (0..BROWSER_LIMIT-1) */

  int8_t STEP = 0;
  int16_t SEEK[MAX_FILES];
  int OPTION = 0;
  int PREVIOUS = 0;
  int32_t VOLUME = 0;
  int32_t BRIGHTNESS = 0;
  const int32_t BRIGHTNESS_COUNT = 10;
  const int32_t BRIGHTNESS_LEVELS[10] = {10,20,30,40,50,60,70,80,90,100};
  int8_t USER;
  int8_t SETTING;
  int8_t COLOR;
  int8_t COVER;
  uint32_t currentDuty;

  char** FILES;
  char** SORTED_FILES = NULL;
  int SORTED_COUNT = 0;
  char** FAVORITES;
  char FAVORITE[256] = "";

  /* Favorites cache for star icons in browser */
  #define FAV_CACHE_MAX 200
  static char *fav_cache[FAV_CACHE_MAX];
  static int   fav_cache_count = 0;

  char** RECENTS;
  char RECENT[256] = "";

  int ROM_COUNTS[COUNT]; /* cached ROM count per system */

  char folder_path[256] = "";

  DIR *directory;
  struct dirent *file;
//}#pragma endregion Global

//{#pragma region Emulator and Directories
  char EMULATORS[COUNT][30] = {
    "SETTINGS (v3.1)",
    "FAVORITES",
    "RECENTLY PLAYED",
    "NINTENDO ENTERTAINMENT SYSTEM",
    "NINTENDO GAME BOY",
    "NINTENDO GAME BOY COLOR",
    "SEGA MASTER SYSTEM",
    "SEGA GAME GEAR",
    "COLECOVISION",
    "ATARI 7800",
    "ZX SPECTRUM",
    "ATARI 2600",
    "ATARI LYNX",
    "PC ENGINE",
    "ATARI 800",
    "SUPER NINTENDO",
    "SEGA GENESIS",
    "NEO GEO",
    "PSRAM APPS"
  };

  char DIRECTORIES[COUNT][10] = {
    "",
    "",
    "",
    "nes",      // nofrendo
    "gb",       // gnuboy
    "gbc",      // gnuboy
    "sms",      // smsplus
    "gg",       // smsplus
    "col",      // smsplus
    "a78",      // prosystem
    "spectrum", // spectrum
    "a26",      // stella
    "lynx",     // handy
    "pce",      // huexpress
    "a800",     // atari800
    "snes",     // snes9x
    "gen",      // gwenesis
    "neogeo",   // gngeo
    "papp"      // psram apps
  };

  const char *SHORT_NAMES[COUNT] = {
    "", "", "",
    "NES", "GB", "GBC", "SMS", "GG", "COL",
    "A78", "ZX", "A26", "LYNX", "PCE",
    "A800", "SNES", "GEN", "NEOGEO", "PAPP"
  };

  char EXTENSIONS[COUNT][10] = {
    "",
    "",
    "",
    "nes",      // nofrendo
    "gb",       // gnuboy
    "gbc",      // gnuboy
    "sms",      // smsplus
    "gg",       // smsplus
    "col",      // smsplus
    "a78",      // prosystem
    "z80",      // spectrum
    "a26",      // stella
    "lnx",      // handy
    "pce",      // huexpress
    "xex",      // atari800
    "smc",      // snes9x
    "md",       // gwenesis
    "zip",      // gngeo (neo geo)
    "papp"      // psram apps
  };

  int LIMIT = 6;
//}#pragma endregion Emulator and Directories

//{#pragma region Buffer
  unsigned short buffer[64000];
//}#pragma endregion Buffer

//{#pragma region PAPP Preview
  static uint16_t *papp_preview_buf = NULL;  /* scaled RGB565, PSRAM-allocated */
  static int papp_preview_w = 0;
  static int papp_preview_h = 0;
  static char papp_preview_name[256] = ""; /* cached filename to avoid reload */
//}#pragma endregion PAPP Preview

/*
  APPLICATION
*/
//{#pragma region Main
  void app_main(void) {

    printf("\n-----\n%s\n-----\n", __func__);
    
    nvs_flash_init();
    odroid_system_init();

    // Audio
    odroid_audio_init(16000);


    VOLUME = odroid_settings_Volume_get();
    odroid_settings_Volume_set(VOLUME);

    //odroid_settings_Backlight_set(BRIGHTNESS);

    // Display
    ili9341_init();
    BRIGHTNESS = get_brightness();
    apply_brightness();

    // Joystick
    odroid_input_gamepad_init();

#ifndef CONFIG_HDMI_OUTPUT
    // Touch panel
    gt911_touch_init(7, 8, -1, -1);
#endif

    // === SAFE MODE: Hold button A during boot ===
    // On ESP32-P4 there is no OTA partition table, so we just
    // reset the launcher state when A is held.
    vTaskDelay(pdMS_TO_TICKS(500));
    {
        odroid_gamepad_state boot_state = odroid_input_read_raw();
        if (boot_state.values[ODROID_INPUT_A]) {
            printf("\n*** SAFE MODE: Button A held during boot ***\n");
            STEP = 0;
            RESTART = false;
            SAFE_MODE = true;
            printf("*** Safe mode active â€” state reset ***\n");
        }
    }

    // Battery
    odroid_input_battery_level_init();

    // SD
    odroid_sdcard_open("/sd");
    create_settings();

    // PSRAM XIP Self-Test (verify PSRAM code execution works)
    psram_app_selftest();

    // Serial file upload (background task on USB Serial JTAG)
    serial_upload_init();

    // Count ROMs per system for carousel display
    count_all_roms();

    // Theme
    get_theme();
    get_restore_states();

    // Toggle
    get_toggle();

    GUI = THEMES[USER];

    //ili9341_prepare();
    //ili9341_clear(0);

    //printf("==============\n%s\n==============\n", "RETRO ESP32");
    switch(esp_reset_reason()) {
      case ESP_RST_POWERON:
        RESTART = false;
        STEP = 1;
        ROMS.offset = 0;
      break;
      case ESP_RST_SW:
        RESTART = true;
      break;
      default:
        RESTART = false;
      break;
    }
    // Safe mode: override any restart/restore if button A was held at boot
    if (SAFE_MODE) {
        RESTART = false;
        STEP = 1;
        ROMS.offset = 0;
    }
    if(RESTART) {
      restart();
      /* After game exit, go directly to browser at the saved position */
      BROWSER = true;
      ROMS.limit = BROWSER_LIMIT;
      folder_path[0] = 0;
      FOLDER = false;
      clear_screen();
      draw_browser_header();

      if (STEP == 1) {
        /* Favorites browser */
        if (ROMS.offset < 0) ROMS.offset = 0;
        if (BROWSER_SEL < 0) BROWSER_SEL = 0;
        get_favorites();
        int visible = ROMS.total - ROMS.offset;
        if (visible > BROWSER_LIMIT) visible = BROWSER_LIMIT;
        if (BROWSER_SEL >= visible) BROWSER_SEL = visible > 0 ? visible - 1 : 0;
      } else if (STEP == 2) {
        /* Recents browser */
        if (ROMS.offset < 0) ROMS.offset = 0;
        if (BROWSER_SEL < 0) BROWSER_SEL = 0;
        get_recents();
        int visible = ROMS.total - ROMS.offset;
        if (visible > BROWSER_LIMIT) visible = BROWSER_LIMIT;
        if (BROWSER_SEL >= visible) BROWSER_SEL = visible > 0 ? visible - 1 : 0;
      } else if (STEP >= 3) {
        load_favorites_cache();
        count_files();
        if (ROMS.total > 0) {
          /* Clamp restored offset and BROWSER_SEL to valid range */
          if (ROMS.offset >= ROMS.total) ROMS.offset = ROMS.total - 1;
          if (ROMS.offset < 0) ROMS.offset = 0;
          int visible = ROMS.total - ROMS.offset;
          if (visible > BROWSER_LIMIT) visible = BROWSER_LIMIT;
          if (BROWSER_SEL >= visible) BROWSER_SEL = visible - 1;
          if (BROWSER_SEL < 0) BROWSER_SEL = 0;
          seek_files();
          draw_browser_screen();
        } else {
          BROWSER_SEL = 0;
          draw_browser_header();
          char msg[64];
          sprintf(msg, "no %s roms found", DIRECTORIES[STEP]);
          int cx = (WIDTH - strlen(msg) * 20) / 2;
          draw_text(cx, 224, msg, false, false, false);
          display_flush();
        }
      } else {
        /* STEP == 0 (settings) â€” shouldn't happen, but fall back to carousel */
        BROWSER = false;
        draw_background();
        restore_layout();
        display_flush();
      }
    } else {
      SPLASH ? splash() : NULL;
      draw_background();
      restore_layout();
      display_flush();
    }
    //xTaskCreate(launcher, "launcher", 8192, NULL, 5, NULL);

    /* Auto-detect unknown USB controller and launch mapping wizard */
    if (odroid_input_usb_gamepad_connected() && !odroid_input_usb_map_exists()) {
      run_map_controller_wizard();
      /* Redraw after wizard */
      draw_background();
      restore_layout();
      display_flush();
    }

    launcher();
  }
//}#pragma endregion Main

/*
  METHODS
*/

//{#pragma region Helpers
  char *remove_ext (char* myStr, char extSep, char pathSep) {
      char *retStr, *lastExt, *lastPath;

      // Error checks and allocate string.

      if (myStr == NULL) return NULL;
      if ((retStr = malloc (strlen (myStr) + 1)) == NULL) return NULL;

      // Make a copy and find the relevant characters.

      strcpy (retStr, myStr);
      lastExt = strrchr (retStr, extSep);
      lastPath = (pathSep == 0) ? NULL : strrchr (retStr, pathSep);

      // If it has an extension separator.

      if (lastExt != NULL) {
          // and it's to the right of the path separator.

          if (lastPath != NULL) {
              if (lastPath < lastExt) {
                  // then remove it.

                  *lastExt = '\0';
              }
          } else {
              // Has extension separator with no path separator.

              *lastExt = '\0';
          }
      }

      // Return the modified string.

      return retStr;
  }

  const char *get_filename (const char* myStr) {
    int ext = '/';
    const char* extension = NULL;
    extension = strrchr(myStr, ext) + 1;

    return extension;
  }

  const char *get_ext (const char* myStr) {
    int ext = '.';
    const char* extension = NULL;
    extension = strrchr(myStr, ext) + 1;

    return extension;
  }

  /* Case-insensitive extension compare (both args should be short extension strings) */
  static bool ext_eq(const char *a, const char *b) {
    for (; *a && *b; a++, b++) {
      if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    }
    return *a == 0 && *b == 0;
  }

  /* Neo Geo: skip support files that aren't game ROMs */
  static bool is_neogeo_system_file(const char *name) {
    return (strcasecmp(name, "neogeo.zip") == 0 ||
            strcasecmp(name, "gngeo_data.zip") == 0);
  }

  bool matches_rom_extension(const char *name, int step) {
    if (name[0] == '.') return false;
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    const char *ext = dot + 1;
    /* Neo Geo (step 17): hide neogeo.zip and gngeo_data.zip */
    if (step == 17 && is_neogeo_system_file(name)) return false;
    if (strlen(EXTENSIONS[step]) > 0 && ext_eq(ext, EXTENSIONS[step])) return true;
    // Atari 800 supports .xex, .atr, and .a52 (Atari 5200 cartridge)
    if (step == 14 && ext_eq(ext, "atr")) return true;
    if (step == 14 && ext_eq(ext, "a52")) return true;
    // Spectrum supports both .z80 and .sna
    if (step == 10 && ext_eq(ext, "sna")) return true;
    // Atari 2600 supports both .a26 and .bin
    if (step == 11 && ext_eq(ext, "bin")) return true;
    // SNES supports both .smc and .sfc
    if (step == 15 && ext_eq(ext, "sfc")) return true;
    // Genesis supports .md and .gen
    if (step == 16 && ext_eq(ext, "gen")) return true;
    return false;
  }

  /*
   * get_ota_slot() â€” Map file extension to OTA partition slot number.
   * Returns -1 for unsupported types.
   *
   * Slot  Partition  Emulator        Extensions
   *  0    ota_0     nofrendo (NES)  .nes
   *  1    ota_1     gnuboy (GB/GBC) .gb .gbc
   *  2    ota_2     smsplus (SMS)   .sms .gg .col
   *  3    ota_3     spectrum (ZX)   .z80 .sna
   *  4    ota_4     stella (A26)    .a26 .bin
   *  5    ota_5     prosystem (A78) .a78
   *  6    ota_6     handy (LNX)     .lnx
   *  7    ota_7     huexpress (PCE) .pce
   *  8    ota_8     atari800 (A800) .xex .atr .a52
   * 10    ota_10    snes9x (SNES)   .smc .sfc
   * 11    ota_11    gwenesis (GEN)  .md .gen
   * 12    ota_12    gngeo (NEOGEO)  .zip
   */
  int get_ota_slot(char* ext) {
    if(ext_eq(ext, "nes")) return 0;   /* ota_0: nofrendo */
    if(ext_eq(ext, "gb"))  return 1;   /* ota_1: gnuboy */
    if(ext_eq(ext, "gbc")) return 1;   /* ota_1: gnuboy */
    if(ext_eq(ext, "sms")) return 2;   /* ota_2: smsplus */
    if(ext_eq(ext, "gg"))  return 2;   /* ota_2: smsplus */
    if(ext_eq(ext, "col")) return 2;   /* ota_2: smsplus */
    if(ext_eq(ext, "z80")) return 3;   /* ota_3: spectrum */
    if(ext_eq(ext, "sna")) return 3;   /* ota_3: spectrum */
    if(ext_eq(ext, "a26")) return 4;   /* ota_4: stella */
    if(ext_eq(ext, "bin")) return 4;   /* ota_4: stella */
    if(ext_eq(ext, "a78")) return 5;   /* ota_5: prosystem */
    if(ext_eq(ext, "lnx")) return 6;   /* ota_6: handy */
    if(ext_eq(ext, "pce")) return 7;   /* ota_7: huexpress */
    if(ext_eq(ext, "xex")) return 8;   /* ota_8: atari800 */
    if(ext_eq(ext, "atr")) return 8;   /* ota_8: atari800 */
    if(ext_eq(ext, "a52")) return 8;   /* ota_8: atari800 (5200 cart) */
    if(ext_eq(ext, "smc")) return 10;  /* ota_10: snes9x */
    if(ext_eq(ext, "sfc")) return 10;  /* ota_10: snes9x */
    if(ext_eq(ext, "md"))  return 11;  /* ota_11: gwenesis */
    if(ext_eq(ext, "gen")) return 11;  /* ota_11: gwenesis */
    if(ext_eq(ext, "zip")) return 12;  /* ota_12: gngeo (neo geo) */
    return -1;
  }

  /* Backward-compat wrapper used by older code paths */
  int get_application(char* ext) {
    int slot = get_ota_slot(ext);
    return slot >= 0 ? slot + 1 : 0;
  }

  /* Map ROM.ext to the correct save-data subdirectory name.
     For normal browsing (STEP >= 3), use DIRECTORIES[STEP].
     For favorites/recents, look up the matching directory. */
  const char* get_save_subdir() {
    if (STEP != 1 && STEP != 2) return DIRECTORIES[STEP];
    for (int i = 3; i < COUNT; i++) {
      if (strlen(EXTENSIONS[i]) > 0 && ext_eq(ROM.ext, EXTENSIONS[i]))
        return DIRECTORIES[i];
    }
    if (ext_eq(ROM.ext, "atr")) return "a800";
    if (ext_eq(ROM.ext, "bin")) return "a26";
    if (ext_eq(ROM.ext, "gen")) return "gen";
    return ROM.ext;
  }
//}#pragma endregion Helpers

//{#pragma region Debounce
  void debounce(int key) {
    display_flush();
    while (gamepad.values[key]) {
      vTaskDelay(pdMS_TO_TICKS(10));
      odroid_input_gamepad_read(&gamepad);
    }
  }
//}#pragma endregion Debounce

//{#pragma region States
  void get_step_state() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    nvs_handle handle;
    err = nvs_open("storage", NVS_READWRITE, &handle);

    err = nvs_get_i8(handle, "STEP", &STEP);
    switch (err) {
      case ESP_OK:
        break;
      case ESP_ERR_NVS_NOT_FOUND:
        STEP = 0;
        break;
      default :
        STEP = 0;
    }
    nvs_close(handle);
    if (STEP < 0 || STEP >= COUNT) STEP = 0;
    //printf("\nGet nvs_get_i8:%d\n", STEP);
  }

  void set_step_state() {
    //printf("\nGet nvs_set_i8:%d\n", STEP);
    nvs_handle handle;
    nvs_open("storage", NVS_READWRITE, &handle);
    nvs_set_i8(handle, "STEP", STEP);
    nvs_commit(handle);
    nvs_close(handle);
  }

  void get_list_state() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    nvs_handle handle;
    err = nvs_open("storage", NVS_READWRITE, &handle);
    err = nvs_get_i16(handle, "LAST", &ROMS.offset);
    switch (err) {
      case ESP_OK:
        break;
      case ESP_ERR_NVS_NOT_FOUND:
        ROMS.offset = 0;
        break;
      default :
        ROMS.offset = 0;
    }
    nvs_close(handle);
    //printf("\nGet nvs_get_i16:%d\n", ROMS.offset);
  }

  void set_list_state() {
    //printf("\nSet nvs_set_i16:%d", ROMS.offset);
    nvs_handle handle;
    nvs_open("storage", NVS_READWRITE, &handle);
    nvs_set_i16(handle, "LAST", ROMS.offset);
    nvs_commit(handle);
    nvs_close(handle);
    get_list_state();
  }

  void get_sel_state() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    nvs_handle handle;
    int16_t val = 0;
    err = nvs_open("storage", NVS_READWRITE, &handle);
    err = nvs_get_i16(handle, "BSEL", &val);
    switch (err) {
      case ESP_OK:
        BROWSER_SEL = (int)val;
        break;
      default:
        BROWSER_SEL = 0;
    }
    nvs_close(handle);
  }

  void set_sel_state() {
    nvs_handle handle;
    nvs_open("storage", NVS_READWRITE, &handle);
    nvs_set_i16(handle, "BSEL", (int16_t)BROWSER_SEL);
    nvs_commit(handle);
    nvs_close(handle);
  }

  void set_restore_states() {
    set_step_state();
    set_list_state();
    set_sel_state();
  }

  void get_restore_states() {
    get_step_state();
    get_list_state();
    get_sel_state();
  }
//}#pragma endregion States

//{#pragma region Text
  int get_letter(char letter) {
    int dx = 0;
    char upper[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!-'&?.,/()[] ";
    char lower[] = "abcdefghijklmnopqrstuvwxyz0123456789!-'&?.,/()[] ";
    for(int n = 0; n < strlen(upper); n++) {
      if(letter == upper[n] || letter == lower[n]) {
        return letter != ' ' ? n * 5 : 0;
        break;
      }
    }
    return dx;
  }

  void draw_text(short x, short y, char *string, bool ext, bool current, bool remove) {
    int length = !ext ? strlen(string) : strlen(string)-(strlen(EXTENSIONS[STEP])+1);
    /* Clamp to visible area: each char is 20px wide (16+4 gap) at 2x scale */
    int max_chars = (WIDTH - 10 - x) / 20;
    if (max_chars < 1) max_chars = 1;
    if (max_chars > 40) max_chars = 40;
    if(length > max_chars){length = max_chars;}
    for(int n = 0; n < length; n++) {
      char ch = string[n];
      if(ch != ' ') {
        int glyph = (ch >= 32 && ch <= 126) ? ch - 32 : '?' - 32;
        int i = 0;
        for(int row = 0; row < 16; row++) {
          uint8_t bits = FONT_8x16[glyph][row];
          for(int sr = 0; sr < 2; sr++) {
            for(int col = 7; col >= 0; col--) {
              uint16_t pixel = (bits & (1 << col))
                ? (current ? GUI.hl : GUI.fg) : GUI.bg;
              if(remove) pixel = GUI.bg;
              buffer[i++] = pixel;
              buffer[i++] = pixel;
            }
          }
        }
        ili9341_write_frame_rectangleLE(x, y, 16, 32, buffer);
      }
      x += ch != ' ' ? 20 : 10;
    }
  }

  /*
   * draw_text_scaled - Draw text at 3x scale of 8x16 font (24x48 per character).
   * Buffer usage per char: 24*48 = 1152 pixels (safe).
   */
  void draw_text_scaled(short x, short y, const char *string, uint16_t color) {
    int length = strlen(string);
    if(length > 26){length = 26;}
    int scale = 3;
    for(int n = 0; n < length; n++) {
      char ch = string[n];
      if(ch != ' ') {
        int glyph = (ch >= 32 && ch <= 126) ? ch - 32 : '?' - 32;
        int i = 0;
        for(int row = 0; row < 16; row++) {
          uint8_t bits = FONT_8x16[glyph][row];
          for(int sr = 0; sr < scale; sr++) {
            for(int col = 7; col >= 0; col--) {
              uint16_t pixel = (bits & (1 << col)) ? color : GUI.bg;
              for(int sc = 0; sc < scale; sc++) {
                buffer[i++] = pixel;
              }
            }
          }
        }
        ili9341_write_frame_rectangleLE(x, y, 8*scale, 16*scale, buffer);
      }
      x += ch != ' ' ? (8*scale)+4 : 15;
    }
  }
//}#pragma endregion Text

//{#pragma region Mask
  void draw_mask(int x, int y, int w, int h){
    int count = w * h;
    if (count > 64000) count = 64000;
    for (int i = 0; i < count; i++) buffer[i] = GUI.bg;
    ili9341_write_frame_rectangleLE(x, y, w, h, buffer);
  }

  void draw_background() {
    int w = WIDTH;
    int h = 60;
    for (int i = 0; i < 8; i++) draw_mask(0, i*h, w, h);
    draw_battery();
    draw_speaker();
    draw_contrast();
    draw_gamepad_icons();
  }

  /* Clear the full screen safely in strips (buffer is 64000 elements!) */
  void clear_screen() {
    int w = WIDTH;
    int h = 60;
    for (int i = 0; i < 8; i++) draw_mask(0, i*h, w, h);
  }
//}#pragma endregion Mask

//{#pragma region ROM Counts
  /* Lightweight scan: count ROM files per system for carousel display */
  void count_all_roms() {
    for (int e = 0; e < COUNT; e++) {
      ROM_COUNTS[e] = 0;

      if (e == 0) continue; /* settings â€“ no count */

      if (e == 1) {
        /* Favorites: count lines in favorites.txt */
        char path[256];
        sprintf(path, "/sd/odroid/data/%s/%s", RETROESP_FOLDER, FAVORITE_FILE);
        FILE *f = fopen(path, "r");
        if (f) {
          char line[256];
          while (fgets(line, sizeof(line), f)) {
            if (line[0] != '\0' && line[0] != '\n') ROM_COUNTS[e]++;
          }
          fclose(f);
        }
        continue;
      }

      if (e == 2) {
        /* Recents: count lines in recent.txt */
        char path[256];
        sprintf(path, "/sd/odroid/data/%s/%s", RETROESP_FOLDER, RECENT_FILE);
        FILE *f = fopen(path, "r");
        if (f) {
          char line[256];
          while (fgets(line, sizeof(line), f)) {
            if (line[0] != '\0' && line[0] != '\n') ROM_COUNTS[e]++;
          }
          fclose(f);
        }
        continue;
      }

      /* Emulator systems (3..13): count files in /sd/roms/{dir}/ */
      char path[256];
      sprintf(path, "/sd/roms/%s", DIRECTORIES[e]);
      DIR *dir = opendir(path);
      if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
          if (ent->d_name[0] == '.') continue;
          if (matches_rom_extension(ent->d_name, e)) {
            ROM_COUNTS[e]++;
          }
        }
        closedir(dir);
      }
    }
  }
//}#pragma endregion ROM Counts

//{#pragma region Settings
  void create_settings() {
    //  printf("\n----- %s START -----", __func__);

    char path[256] = "/sd/odroid/data";
    sprintf(path, "%s/%s", path, RETROESP_FOLDER);

    //  printf("\npath:%s", path);

    /*
    if(directory != NULL) {
      free(directory);
      closedir(directory);
    }
    */

    struct stat st;
    if (stat(path, &st) == -1) {mkdir(path, 0777);}
    create_favorites();
    create_recents();
    /*
    directory = opendir(path);
    if(directory) {
      create_favorites();
      create_recents();
      free(directory);
      closedir(directory);
    }
    */
    // printf("\n----- %s END -----", __func__);
  }

  void draw_settings() {
    int x = ORIGIN.x;
    int y = POS.y + 92;

    draw_mask(x,y-1,400,38);
    draw_text(x,y,(char *)"CLEAR RECENTS",false, SETTING == 0 ? true : false, false);

    y+=42;
    draw_mask(x,y-1,400,38);
    draw_text(x,y,(char *)"VOLUME",false, SETTING == 1 ? true : false, false);

    draw_volume();

    y+=42;
    draw_mask(x,y-1,400,38);
    draw_text(x,y,(char *)"BRIGHTNESS",false, SETTING == 2 ? true : false, false);

    draw_brightness();

    y+=42;
    draw_mask(x,y-1,400,38);
    draw_text(x,y,(char *)"MAP CONTROLLER",false, SETTING == 3 ? true : false, false);

    display_flush();
  }

  /* ─── MAP CONTROLLER wizard ─────────────────────────────────────── */
  void run_map_controller_wizard(void)
  {
    if (!odroid_input_usb_gamepad_connected()) {
      /* No controller — show message and return */
      clear_screen();
      draw_text(40, 200, (char *)"CONNECT USB CONTROLLER", false, true, false);
      display_flush();
      vTaskDelay(pdMS_TO_TICKS(2000));
      return;
    }

    /* Button names in mapping order matching odroid_usb_map_t indices:
       0=A, 1=B, 2=X, 3=Y, 4=L, 5=R, 6=SELECT, 7=START,
       8=UP, 9=DOWN, 10=LEFT, 11=RIGHT */
    static const char *btn_names[ODROID_USB_MAP_COUNT] = {
      "A", "B", "X", "Y", "L", "R", "SELECT", "START",
      "D-PAD UP", "D-PAD DOWN", "D-PAD LEFT", "D-PAD RIGHT"
    };

    odroid_usb_map_t new_map;
    memset(&new_map, 0, sizeof(new_map));

    for (int i = 0; i < ODROID_USB_MAP_COUNT; i++) {
      bool is_dpad = (i >= 8);

      clear_screen();
      char prompt[40];
      snprintf(prompt, sizeof(prompt), "PRESS: %s", btn_names[i]);
      draw_text(40, 180, prompt, false, true, false);

      /* Sub-text showing progress */
      char progress[32];
      snprintf(progress, sizeof(progress), "(%d of %d)", i + 1, ODROID_USB_MAP_COUNT);
      draw_text(40, 230, progress, false, false, false);
      display_flush();

      /* Wait for all inputs released first */
      gamepad_state_t gp;
      do {
        vTaskDelay(pdMS_TO_TICKS(50));
        gamepad_get_state(&gp);
      } while (gp.buttons != 0 || gp.dpad != 0 ||
               gp.axis_lx < -96 || gp.axis_lx > 96 ||
               gp.axis_ly < -96 || gp.axis_ly > 96);

      /* Wait for an input */
      uint32_t pressed = 0;
      bool detected = false;
      while (!detected) {
        vTaskDelay(pdMS_TO_TICKS(30));
        gamepad_get_state(&gp);
        if (!gp.connected) {
          /* Controller disconnected mid-wizard */
          clear_screen();
          draw_text(40, 200, (char *)"CONTROLLER DISCONNECTED", false, true, false);
          display_flush();
          vTaskDelay(pdMS_TO_TICKS(2000));
          return;
        }
        /* Check buttons (works for both regular buttons and d-pad-as-buttons) */
        if (gp.buttons != 0) {
          pressed = gp.buttons;
          detected = true;
        }
        /* For d-pad entries, also accept hat switch and analog stick */
        if (is_dpad && !detected) {
          if (gp.dpad != 0 ||
              gp.axis_lx < -96 || gp.axis_lx > 96 ||
              gp.axis_ly < -96 || gp.axis_ly > 96) {
            pressed = 0;  /* 0 = use built-in hat/axis detection */
            detected = true;
          }
        }
      }

      new_map.btn[i] = pressed;

      /* Brief flash to confirm */
      clear_screen();
      snprintf(prompt, sizeof(prompt), "%s: OK", btn_names[i]);
      draw_text(40, 200, prompt, false, true, false);
      display_flush();
      vTaskDelay(pdMS_TO_TICKS(300));
    }

    /* Save to SD */
    odroid_sdcard_open("/sd");
    bool saved = odroid_input_usb_map_save(&new_map);
    odroid_sdcard_close();

    clear_screen();
    if (saved) {
      draw_text(40, 200, (char *)"MAPPING SAVED", false, true, false);
    } else {
      draw_text(40, 200, (char *)"SAVE FAILED", false, true, false);
    }
    display_flush();
    vTaskDelay(pdMS_TO_TICKS(1500));
  }

//}#pragma endregion Settings

//{#pragma region Toggle
  void draw_toggle() {
    get_toggle();
    int x = SCREEN.w - 76;
    int y = POS.y + 134 + 6;
    int w, h;

    int i = 0;
    for(h = 0; h < 9; h++) {
      for(int sh = 0; sh < 2; sh++) {
        for(w = 0; w < 18; w++) {
          uint16_t pixel = toggle[h + (COLOR*9)][w] == 0 ? 
          GUI.bg : toggle[h + (COLOR*9)][w] == WHITE ? 
          SETTING == 1 ? GUI.hl : GUI.fg : toggle[h + (COLOR*9)][w];
          buffer[i++] = pixel;
          buffer[i++] = pixel;
        }
      }
    }
    ili9341_write_frame_rectangleLE(x, y, 36, 18, buffer);
  }

  void set_toggle() {
    COLOR = COLOR == 0 ? 1 : 0;
    nvs_handle handle;
    nvs_open("storage", NVS_READWRITE, &handle);
    nvs_set_i8(handle, "COLOR", COLOR);
    nvs_commit(handle);
    nvs_close(handle);
  }

  void get_toggle() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    nvs_handle handle;
    err = nvs_open("storage", NVS_READWRITE, &handle);

    err = nvs_get_i8(handle, "COLOR", &COLOR);
    switch (err) {
      case ESP_OK:
        break;
      case ESP_ERR_NVS_NOT_FOUND:
        COLOR = 1;
        break;
      default :
        COLOR = 1;
    }
    nvs_close(handle);
  }

  void draw_cover_toggle() {
    get_cover_toggle();
    int x = SCREEN.w - 76;
    int y = POS.y + 260 + 6;
    int w, h;

    int i = 0;
    for(h = 0; h < 9; h++) {
      for(int sh = 0; sh < 2; sh++) {
        for(w = 0; w < 18; w++) {
          uint16_t pixel = toggle[h + (COVER*9)][w] == 0 ? 
          GUI.bg : toggle[h + (COVER*9)][w] == WHITE ? 
          SETTING == 4 ? GUI.hl : GUI.fg : toggle[h + (COVER*9)][w];
          buffer[i++] = pixel;
          buffer[i++] = pixel;
        }
      }
    }
    ili9341_write_frame_rectangleLE(x, y, 36, 18, buffer);
  }

  void set_cover_toggle() {
    COVER = COVER == 0 ? 1 : 0;
    nvs_handle handle;
    nvs_open("storage", NVS_READWRITE, &handle);
    nvs_set_i8(handle, "COVER", COVER);
    nvs_commit(handle);
    nvs_close(handle);
  }

  void get_cover_toggle() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    nvs_handle handle;
    err = nvs_open("storage", NVS_READWRITE, &handle);

    err = nvs_get_i8(handle, "COVER", &COVER);
    switch (err) {
      case ESP_OK:
        break;
      case ESP_ERR_NVS_NOT_FOUND:
        COVER = false;
        break;
      default :
        COVER = false;
    }
    nvs_close(handle);
  }
//}#pragma endregion Toggle

//{#pragma region Volume
  void draw_volume() {
    int32_t volume = get_volume();
    int x = SCREEN.w - 240;
    int y = POS.y + 134 + 9;
    int w, h;

    int i = 0;
    for(h = 0; h < 14; h++) {
      for(w = 0; w < 200; w++) {
        buffer[i] = (w+h)%2 == 0 ? GUI.fg : GUI.bg;
        i++;
      }
    }
    ili9341_write_frame_rectangleLE(x, y, 200, 14, buffer);

    if(volume > 0) {
      i = 0;
      for(h = 0; h < 14; h++) {
        for(w = 0; w < (25 * volume); w++) {
          if(SETTING == 1) {
            buffer[i] = GUI.hl;
          } else {
            buffer[i] = GUI.fg;
          }
          i++;
        }
      }
      ili9341_write_frame_rectangleLE(x, y, (25 * volume), 14, buffer);
    }

    draw_speaker();
  }
  int32_t get_volume() {
    return odroid_settings_Volume_get();
  }
  void set_volume() {
    odroid_settings_Volume_set(VOLUME);
    draw_volume();
    display_flush();
  }
//}#pragma endregion Volume

//{#pragma region Brightness
  void draw_brightness() {
    int x = SCREEN.w - 240;
    int y = POS.y + 176 + 9;
    int w, h;

    int i = 0;
    for(h = 0; h < 14; h++) {
      for(w = 0; w < 200; w++) {
        buffer[i] = (w+h)%2 == 0 ? GUI.fg : GUI.bg;
        i++;
      }
    }
    ili9341_write_frame_rectangleLE(x, y, 200, 14, buffer);

    //if(BRIGHTNESS > 0) {
      i = 0;
      int bw = (BRIGHTNESS_COUNT * BRIGHTNESS * 2)+BRIGHTNESS*2+2;
      for(h = 0; h < 14; h++) {
        for(w = 0; w < bw; w++) {
          if(SETTING == 2) {
            buffer[i] = GUI.hl;
          } else {
            buffer[i] = GUI.fg;
          }
          i++;
        }
      }
      ili9341_write_frame_rectangleLE(x, y, bw, 14, buffer);
    //}
    draw_contrast();
  }
  int32_t get_brightness() {
    return odroid_settings_Backlight_get();
  }
  void set_brightness() {
    odroid_settings_Backlight_set(BRIGHTNESS);
    draw_brightness();
    apply_brightness();
    display_flush();
  }
  void apply_brightness() {
    const int DUTY_MAX = 0x1fff;
    BRIGHTNESS = get_brightness();
    int duty = DUTY_MAX * (BRIGHTNESS_LEVELS[BRIGHTNESS] * 0.01f);

    if(is_backlight_initialized()) {
      currentDuty = ledc_get_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
      if (currentDuty != duty) {
        //ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, currentDuty);
        //ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        //ledc_set_fade_with_time(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty, 1000);
        //ledc_fade_start(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, LEDC_FADE_WAIT_DONE /*LEDC_FADE_NO_WAIT|LEDC_FADE_WAIT_DONE|LEDC_FADE_MAX*/);
        //ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        //ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
        ledc_set_fade_time_and_start(
          LEDC_LOW_SPEED_MODE,
          LEDC_CHANNEL_0,
          duty,
            25,
          LEDC_FADE_WAIT_DONE
        );
      }
    }
  }
//}#pragma endregion Brightness

//{#pragma region Theme
  void draw_themes() {
    int x = ORIGIN.x;
    int y = POS.y + 92;
    int filled = 0;
    int count = 22;
    for(int n = USER; n < count; n++){
      if(filled < ROMS.limit) {
        draw_mask(x,y-1,400,38);
        draw_text(x,y,THEMES[n].name,false, n == USER ? true : false, false);
        y+=42;
        filled++;
      }
    }
    int slots = (ROMS.limit - filled);
    for(int n = 0; n < slots; n++) {
      draw_mask(x,y-1,400,38);
      draw_text(x,y,THEMES[n].name,false,false, false);
      y+=42;
    }
    display_flush();
  }

  void get_theme() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    nvs_handle handle;
    err = nvs_open("storage", NVS_READWRITE, &handle);

    err = nvs_get_i8(handle, "USER", &USER);
    switch (err) {
      case ESP_OK:
        break;
      case ESP_ERR_NVS_NOT_FOUND:
        USER = 18;
        set_theme(USER);
        break;
      default :
        USER = 18;
        set_theme(USER);
    }
    nvs_close(handle);
  }

  void set_theme(int8_t i) {
    nvs_handle handle;
    nvs_open("storage", NVS_READWRITE, &handle);
    nvs_set_i8(handle, "USER", i);
    nvs_commit(handle);
    nvs_close(handle);
    get_theme();
  }

  void update_theme() {
    GUI = THEMES[USER];
    set_theme(USER);
    draw_background();
    draw_mask(0, 0, WIDTH, 60);
    draw_mask(0, 60, WIDTH, 60);
    draw_mask(0, 120, WIDTH, 40);
    draw_systems();
    draw_text(16,4,EMULATORS[STEP], false, true, false);
    draw_themes();
  }
//}#pragma endregion Theme

//{#pragma region GUI
  void draw_systems() {
    for(int e = 0; e < COUNT; e++) {
      int i = 0;
      int x = SYSTEMS[e].x;
      int y = POS.y;
      int w = 64;
      int h = 64;
      if(x > 0 && x < 736) {
        for(int r = 0; r < 32; r++) {
          for(int sr = 0; sr < 2; sr++) {
            for(int c = 0; c < 32; c++) {
              uint16_t pixel = GUI.bg;
              switch(COLOR) {
                case 0:
                  pixel = (*SYSTEMS[e].system)[r][c] == WHITE ? GUI.hl : GUI.bg;
                break;
                case 1:
                  pixel = (*SYSTEMS[e].color)[r][c] == 0 ? GUI.bg : (*SYSTEMS[e].color)[r][c];
                break;
              }
              buffer[i++] = pixel;
              buffer[i++] = pixel;
            }
          }
        }
        ili9341_write_frame_rectangleLE(x, y, w, h, buffer);

        /* Draw short name + ROM count below the icon at 1x scale (skip settings at index 0) */
        if (e > 0 && ROM_COUNTS[e] >= 0) {
          char cnt[24];
          if (SHORT_NAMES[e][0])
            sprintf(cnt, "%s (%d)", SHORT_NAMES[e], ROM_COUNTS[e]);
          else
            sprintf(cnt, "(%d)", ROM_COUNTS[e]);
          int len = strlen(cnt);
          int char_w = 9;  /* 8px glyph + 1px gap at 1x scale */
          int tx = x + (64 - len * char_w) / 2;
          if (tx < 0) tx = 0;
          /* Draw each char at 1x scale (8x16 pixels) */
          int cx = tx;
          for (int ci = 0; ci < len; ci++) {
            char ch = cnt[ci];
            if (ch != ' ') {
              int glyph = (ch >= 32 && ch <= 126) ? ch - 32 : '?' - 32;
              int bi = 0;
              for (int row = 0; row < 16; row++) {
                uint8_t bits = FONT_8x16[glyph][row];
                for (int col = 7; col >= 0; col--) {
                  buffer[bi++] = (bits & (1 << col)) ? GUI.fg : GUI.bg;
                }
              }
              ili9341_write_frame_rectangleLE(cx, y + 70, 8, 16, buffer);
            }
            cx += char_w;
          }
        }
      }
    }
  }

  /*
   * draw_system_logo - Draw a 6x scaled (192x192) version of the current
   * system's icon, centered horizontally.
   * Buffer usage: 192*192 = 36864 pixels (safe, buffer is 64000).
   */
  void draw_system_logo() {
    if (STEP == 0) return; /* no big logo for settings screen */
    int scale = 6;
    int w = 32 * scale; /* 192 */
    int h = 32 * scale; /* 192 */
    int x = (WIDTH - w) / 2; /* 304 */
    int y = 144;
    uint16_t red = 0xF800; /* intense red in RGB565 */
    int i = 0;
    for (int r = 0; r < 32; r++) {
      for (int sr = 0; sr < scale; sr++) {
        for (int c = 0; c < 32; c++) {
          uint16_t pixel = GUI.bg;
          switch(COLOR) {
            case 0:
              pixel = (*SYSTEMS[STEP].system)[r][c] == WHITE ? red : GUI.bg;
            break;
            case 1: {
              uint16_t cpx = (*SYSTEMS[STEP].color)[r][c];
              pixel = cpx != 0 ? cpx : GUI.bg;
            } break;
          }
          for (int sc = 0; sc < scale; sc++) {
            buffer[i++] = pixel;
          }
        }
      }
    }
    ili9341_write_frame_rectangleLE(x, y, w, h, buffer);
  }

  /*
   * load_artwork_to_buf - Load & convert a system artwork PNG into an 800x450
   * PSRAM buffer (RGB565, ready to blit at y=30). Returns the allocated buffer
   * on success (caller must free), or NULL on failure.
   */
  static uint16_t *load_artwork_to_buf(int step) {
    if (step == 0) return NULL;

    char path[64];
    if (step == 1)
      snprintf(path, sizeof(path), "/sd/system_art/favorite.png");
    else if (step == 2)
      snprintf(path, sizeof(path), "/sd/system_art/recent.png");
    else
      snprintf(path, sizeof(path), "/sd/system_art/%s.png", DIRECTORIES[step]);

    pngObject png = {0};
    if (!loadPngFromFileRaw(path, &png, true, true)) return NULL;
    if (png.w == 0 || png.h == 0 || !png.data) return NULL;

    uint16_t *src = (uint16_t *)png.data;
    int pw = png.w, ph = png.h;
    /* Stretch to fill exactly WIDTH x (HEIGHT-30) */
    int dst_w = WIDTH;
    int dst_h = HEIGHT - 30;

    uint16_t *art = (uint16_t *)heap_caps_malloc(dst_w * dst_h * 2, MALLOC_CAP_SPIRAM);
    if (!art) { free(src); return NULL; }

    /* Nearest-neighbor scale with byte-swap + R<->B swap */
    for (int r = 0; r < dst_h; r++) {
      int sr = r * ph / dst_h;
      uint16_t *srow = &src[sr * pw];
      uint16_t *drow = &art[r * dst_w];
      for (int c = 0; c < dst_w; c++) {
        int sc = c * pw / dst_w;
        uint16_t p = srow[sc];
        uint16_t bs = (p >> 8) | (p << 8);
        drow[c] = ((bs & 0x1F) << 11) | (bs & 0x07E0) | ((bs >> 11) & 0x1F);
      }
    }
    free(src);
    return art;
  }

  /*
   * show_system_artwork - Load per-emulator PNG artwork from SD and display it.
   * Path: /sd/system_art/{name}.png  (e.g. /sd/system_art/nes.png)
   * PNGs are 400x225, software-upscaled 2x to 800x450 filling y=30..479.
   * A 30px black header stripe at y=0 shows the file count (yellow, left)
   * and battery/volume/brightness icons (right).
   * Returns 1 on success, 0 on failure (caller should fall back to draw_system_logo).
   */
  static int show_system_artwork(void) {
    if (STEP == 0) return 0; /* no artwork for settings */

    char path[64];
    if (STEP == 1)
      snprintf(path, sizeof(path), "/sd/system_art/favorite.png");
    else if (STEP == 2)
      snprintf(path, sizeof(path), "/sd/system_art/recent.png");
    else
      snprintf(path, sizeof(path), "/sd/system_art/%s.png", DIRECTORIES[STEP]);

    pngObject png = {0};
    if (!loadPngFromFileRaw(path, &png, true, true)) return 0;
    if (png.w == 0 || png.h == 0 || !png.data) return 0;

    uint16_t *src = (uint16_t *)png.data;
    int pw = png.w;
    int ph = png.h;
    /* Stretch to fill exactly WIDTH x (HEIGHT-30) */
    int dst_w = WIDTH;
    int dst_h = HEIGHT - 30;

    int blit_x = 0;
    int blit_y = 30;

    /* NN upscale in strips that fit buffer[64000]
       Byte-swap + R<->B swap for PNGdec big-endian RGB565 */
    int max_dst_rows = 64000 / dst_w;
    if (max_dst_rows < 1) max_dst_rows = 1;
    for (int dr = 0; dr < dst_h; dr += max_dst_rows) {
      int rows = (dr + max_dst_rows <= dst_h) ? max_dst_rows : (dst_h - dr);
      int bi = 0;
      for (int r = 0; r < rows; r++) {
        int sr = (dr + r) * ph / dst_h;
        uint16_t *srow = &src[sr * pw];
        for (int c = 0; c < dst_w; c++) {
          int sc = c * pw / dst_w;
          uint16_t p = srow[sc];
          uint16_t bs = (p >> 8) | (p << 8);
          buffer[bi++] = ((bs & 0x1F) << 11) | (bs & 0x07E0) | ((bs >> 11) & 0x1F);
        }
      }
      ili9341_write_frame_rectangleLE(blit_x, blit_y + dr, dst_w, rows, buffer);
    }
    free(src);

    /* 30px black header stripe */
    for (int i = 0; i < WIDTH * 30; i++) buffer[i] = 0x0000;
    ili9341_write_frame_rectangleLE(0, 0, WIDTH, 30, buffer);

    /* File count in yellow on the left */
    {
      uint16_t saved_bg = GUI.bg, saved_fg = GUI.fg;
      GUI.bg = 0x0000;
      GUI.fg = 0xFFE0; /* yellow */
      char cnt[32];
      if (SHORT_NAMES[STEP][0])
        sprintf(cnt, "%s (%d files)", SHORT_NAMES[STEP], ROM_COUNTS[STEP]);
      else
        sprintf(cnt, "%d files", ROM_COUNTS[STEP]);
      draw_text(8, 0, cnt, false, false, false);
      GUI.bg = saved_bg;
      GUI.fg = saved_fg;
    }

    /* Battery / speaker / contrast icons on the right (y=0, black bg) */
    {
      uint16_t saved_bg = GUI.bg;
      GUI.bg = 0x0000;

      /* Contrast */
      {
        int32_t dy = 0;
        switch (BRIGHTNESS) {
          case 10: case 9: case 8: dy = 0; break;
          case 7: case 6: case 5: dy = 16; break;
          case 4: case 3: case 2: dy = 32; break;
          default: dy = 48; break;
        }
        int i = 0, x = SCREEN.w - 144;
        for (int h = 0; h < 16; h++) {
          for (int sh = 0; sh < 2; sh++) {
            for (int w = 0; w < 16; w++) {
              buffer[i++] = brightness[dy + h][w] == WHITE ? GUI.hl : 0x0000;
              buffer[i++] = brightness[dy + h][w] == WHITE ? GUI.hl : 0x0000;
            }
          }
        }
        ili9341_write_frame_rectangleLE(x, 0, 32, 32, buffer);
      }

      /* Speaker */
      {
        int32_t vol = get_volume();
        int dh = 0;
        switch (vol) {
          case 0: dh = 64; break;
          case 1: case 2: case 3: dh = 48; break;
          case 4: case 5: dh = 32; break;
          case 6: case 7: dh = 16; break;
          default: dh = 0; break;
        }
        int i = 0, x = SCREEN.w - 104;
        for (int h = 0; h < 16; h++) {
          for (int sh = 0; sh < 2; sh++) {
            for (int w = 0; w < 16; w++) {
              buffer[i++] = speaker[dh + h][w] == WHITE ? GUI.hl : 0x0000;
              buffer[i++] = speaker[dh + h][w] == WHITE ? GUI.hl : 0x0000;
            }
          }
        }
        ili9341_write_frame_rectangleLE(x, 0, 32, 32, buffer);
      }

      /* Battery */
      #ifdef BATTERY
      {
        odroid_input_battery_level_read(&battery_state);
        int i = 0, x = SCREEN.w - 64;
        for (int h = 0; h < 16; h++) {
          for (int sh = 0; sh < 2; sh++) {
            for (int w = 0; w < 16; w++) {
              buffer[i++] = battery[h][w] == WHITE ? GUI.hl : 0x0000;
              buffer[i++] = battery[h][w] == WHITE ? GUI.hl : 0x0000;
            }
          }
        }
        ili9341_write_frame_rectangleLE(x, 0, 32, 32, buffer);

        int pct = battery_state.percentage / 10;
        int bw = pct > 0 ? (pct > 10 ? 10 : pct) : 10;
        int color[11] = {24576,24576,64288,64288,65504,65504,65504,26592,26592,26592,26592};
        i = 0;
        for (int c = 0; c < 8; c++)
          for (int n = 0; n <= bw * 2; n++)
            buffer[i++] = color[bw];
        ili9341_write_frame_rectangleLE(x + 4, 12, bw * 2, 8, buffer);
      }
      #endif

      /* USB gamepad icon */
      {
        bool usb = odroid_input_usb_gamepad_connected();
        int i = 0, x = SCREEN.w - 184;
        for (int h = 0; h < 16; h++) {
          for (int sh = 0; sh < 2; sh++) {
            for (int w = 0; w < 16; w++) {
              buffer[i++] = usb_gamepad_icon[h][w] == WHITE
                            ? (usb ? GUI.hl : 0x4208) : 0x0000;
              buffer[i++] = usb_gamepad_icon[h][w] == WHITE
                            ? (usb ? GUI.hl : 0x4208) : 0x0000;
            }
          }
        }
        ili9341_write_frame_rectangleLE(x, 0, 32, 32, buffer);
      }

      /* GPIO gamepad icon */
      {
        bool gpio = odroid_input_gpio_pad_detected();
        int i = 0, x = SCREEN.w - 224;
        for (int h = 0; h < 16; h++) {
          for (int sh = 0; sh < 2; sh++) {
            for (int w = 0; w < 16; w++) {
              buffer[i++] = gpio_gamepad_icon[h][w] == WHITE
                            ? (gpio ? GUI.hl : 0x4208) : 0x0000;
              buffer[i++] = gpio_gamepad_icon[h][w] == WHITE
                            ? (gpio ? GUI.hl : 0x4208) : 0x0000;
            }
          }
        }
        ili9341_write_frame_rectangleLE(x, 0, 32, 32, buffer);
      }

      GUI.bg = saved_bg;
    }

    return 1;
  }

  void draw_folder(int x, int y, bool current) {
    int i = 0;
    for(int h = 0; h < 16; h++) {
      for(int sh = 0; sh < 2; sh++) {
        for(int w = 0; w < 16; w++) {
          uint16_t pixel = folder[h][w] == WHITE ? current ? GUI.hl : GUI.fg : GUI.bg;
          buffer[i++] = pixel;
          buffer[i++] = pixel;
        }
      }
    }
    ili9341_write_frame_rectangleLE(x, y, 32, 32, buffer);
  }

  void draw_media(int x, int y, bool current, int offset) {
    if(offset == -1) {offset = (STEP-3) * 16;}
    int i = 0;
    for(int h = 0; h < 16; h++) {
      for(int sh = 0; sh < 2; sh++) {
        for(int w = offset; w < (offset+16); w++) {
          uint16_t pixel = GUI.bg;
          switch(COLOR) {
            case 0:
              pixel = media[h][w] == WHITE ? current ? GUI.hl : GUI.fg : GUI.bg;
            break;
            case 1:
              pixel = media_color[h][w] == 0 ? GUI.bg : media_color[h][w];
              if(current) {
                pixel = media_color[h+16][w] == 0 ? GUI.bg : media_color[h+16][w];
              }
            break;
          }
          buffer[i++] = pixel;
          buffer[i++] = pixel;
        }
      }
    }
    ili9341_write_frame_rectangleLE(x, y, 32, 32, buffer);
  }

  void draw_battery() {
    #ifdef BATTERY
      odroid_input_battery_level_read(&battery_state);

      int i = 0;
      int x = SCREEN.w - 64;
      int y = 16;
      int h = 0;
      int w = 0;

      draw_mask(x,y,32,32);
      for(h = 0; h < 16; h++) {
        for(int sh = 0; sh < 2; sh++) {
          for(w = 0; w < 16; w++) {
            uint16_t pixel = battery[h][w] == WHITE ? GUI.hl : GUI.bg;
            buffer[i++] = pixel;
            buffer[i++] = pixel;
          }
        }
      }
      ili9341_write_frame_rectangleLE(x, y, 32, 32, buffer);

      int percentage = battery_state.charging ? 100 : battery_state.percentage;
      percentage /= 10;
      x += 4;
      y += 12;
      w = percentage > 0 ? percentage > 10 ? 10 : percentage : 10;
      h = 8;
      i = 0;

      int color[11] = {24576,24576,64288,64288,65504,65504,65504,26592,26592,26592,26592};

      int fill = color[w];
      for(int c = 0; c < h; c++) {
        for(int n = 0; n <= w*2; n++) {
          buffer[i] = fill;
          i++;
        }
      }
      ili9341_write_frame_rectangleLE(x, y, w*2, h, buffer);
    #endif
  }

  void draw_speaker() {
    int32_t volume = get_volume();

    int i = 0;
    int x = SCREEN.w - 104;
    int y = 16;
    int h = 16;
    int w = 16;

    draw_mask(x,y,32,32);

    int dh = 0;
    switch(volume) {
      case 0:dh = 64;break;
      case 1:case 2:case 3:dh = 48;break;
      case 4:case 5:dh = 32;break;
      case 6:case 7:dh = 16;break;
      case 9:dh = 0;break;
    }
    for(h = 0; h < 16; h++) {
      for(int sh = 0; sh < 2; sh++) {
        for(w = 0; w < 16; w++) {
          uint16_t pixel = speaker[dh+h][w] == WHITE ? GUI.hl : GUI.bg;
          buffer[i++] = pixel;
          buffer[i++] = pixel;
        }
      }
    }
    ili9341_write_frame_rectangleLE(x, y, 32, 32, buffer);
  }

  void draw_contrast() {
    int32_t dy = 0;
    switch(BRIGHTNESS) {
      case 10:
      case 9:
      case 8:
        dy = 0;
      break;
      case 7:
      case 6:
      case 5:
        dy = 16;
      break;
      case 4:
      case 3:
      case 2:
        dy = 32;
      break;
      case 1:
      case 0:
        dy = 48;
      break;
    }
    int i = 0;
    int x = SCREEN.w - 144;
    int y = 16;
    int h = 16;
    int w = 16;

    draw_mask(x,y,32,32);

    for(h = 0; h < 16; h++) {
      for(int sh = 0; sh < 2; sh++) {
        for(w = 0; w < 16; w++) {
          uint16_t pixel = brightness[dy+h][w] == WHITE ? GUI.hl : GUI.bg;
          buffer[i++] = pixel;
          buffer[i++] = pixel;
        }
      }
    }
    ili9341_write_frame_rectangleLE(x, y, 32, 32, buffer);
  }

  void draw_gamepad_icons() {
    /* USB gamepad icon at SCREEN.w - 184 */
    {
      int i = 0;
      int x = SCREEN.w - 184;
      int y = 16;
      draw_mask(x, y, 32, 32);
      bool usb = odroid_input_usb_gamepad_connected();
      for (int h = 0; h < 16; h++) {
        for (int sh = 0; sh < 2; sh++) {
          for (int w = 0; w < 16; w++) {
            uint16_t pixel = usb_gamepad_icon[h][w] == WHITE
                             ? (usb ? GUI.hl : GUI.fg) : GUI.bg;
            buffer[i++] = pixel;
            buffer[i++] = pixel;
          }
        }
      }
      ili9341_write_frame_rectangleLE(x, y, 32, 32, buffer);
    }
    /* GPIO gamepad icon at SCREEN.w - 224 */
    {
      int i = 0;
      int x = SCREEN.w - 224;
      int y = 16;
      draw_mask(x, y, 32, 32);
      bool gpio = odroid_input_gpio_pad_detected();
      for (int h = 0; h < 16; h++) {
        for (int sh = 0; sh < 2; sh++) {
          for (int w = 0; w < 16; w++) {
            uint16_t pixel = gpio_gamepad_icon[h][w] == WHITE
                             ? (gpio ? GUI.hl : GUI.fg) : GUI.bg;
            buffer[i++] = pixel;
            buffer[i++] = pixel;
          }
        }
      }
      ili9341_write_frame_rectangleLE(x, y, 32, 32, buffer);
    }
  }

  void draw_numbers() {
    int x = WIDTH - 20;
    int y = POS.y + 96;
    int w = 0;
    char count[10];
    sprintf(count, "(%d/%d)", (ROMS.offset+1), ROMS.total);
    for (const char *p = count; *p; p++) w += (*p == ' ') ? 10 : 20;
    x -= w;
    draw_text(x,y,count,false,false, false);
  }

  void delete_numbers() {
    int x = WIDTH - 20;
    int y = POS.y + 96;
    int w = 0;
    char count[10];
    sprintf(count, "(%d/%d)", (ROMS.offset+1), ROMS.total);
    for (const char *p = count; *p; p++) w += (*p == ' ') ? 10 : 20;
    x -= w;
    draw_text(x,y,count,false,false, true);
  }

  void draw_launcher() {
    draw_background();
    draw_text(40,4,EMULATORS[STEP], false, true, false);
    int i = 0;
    int x = GAP/3;
    int y = POS.y;
    int w = 64;
    int h = 64;
    for(int r = 0; r < 32; r++) {
      for(int sr = 0; sr < 2; sr++) {
        for(int c = 0; c < 32; c++) {
          uint16_t pixel = GUI.bg;
          switch(COLOR) {
            case 0:
              pixel = (*SYSTEMS[STEP].system)[r][c] == WHITE ? GUI.hl : GUI.bg;
            break;
            case 1:
              pixel = (*SYSTEMS[STEP].color)[r][c] == 0 ? GUI.bg : (*SYSTEMS[STEP].color)[r][c];
            break;
          }
          buffer[i++] = pixel;
          buffer[i++] = pixel;
        }
      }
    }
    ili9341_write_frame_rectangleLE(x, y, w, h, buffer);

    y += 96;
    int offset = -1;
    if(STEP == 1 || STEP == 2) {
      if(ext_eq(ROM.ext, "nes")) {offset = 0*16;}
      if(ext_eq(ROM.ext, "gb")) {offset = 1*16;}
      if(ext_eq(ROM.ext, "gbc")) {offset = 2*16;}
      if(ext_eq(ROM.ext, "sms")) {offset = 3*16;}
      if(ext_eq(ROM.ext, "gg")) {offset = 4*16;}
      if(ext_eq(ROM.ext, "col")) {offset = 5*16;}
      if(ext_eq(ROM.ext, "z80")) {offset = 6*16;}
      if(ext_eq(ROM.ext, "a26")) {offset = 7*16;}
      if(ext_eq(ROM.ext, "a78")) {offset = 8*16;}
      if(ext_eq(ROM.ext, "lnx")) {offset = 9*16;}
      if(ext_eq(ROM.ext, "pce")) {offset = 10*16;}
      if(ext_eq(ROM.ext, "xex")) {offset = 7*16;}
      if(ext_eq(ROM.ext, "atr")) {offset = 7*16;}
    }
    draw_media(x,y-12,true,offset);
    draw_launcher_options();
    get_cover_toggle();
    if(COVER == 1){get_cover();}
    display_flush();
  }

  void draw_launcher_options() {
    has_save_file(ROM.name);

    char favorite[256] = "";
    sprintf(favorite, "%s/%s", ROM.path, ROM.name);
    is_favorite(favorite);
    int x = GAP/3 + 64;
    int y = POS.y + 96;
    int w = 10;
    int h = 10;
    int i = 0;
    int offset = 0;
    if(SAVED) {
      // resume
      i = 0;
      offset = 5;
      for(int r = 0; r < 5; r++){for(int sr = 0; sr < 2; sr++){for(int c = 0; c < 5; c++) {
        uint16_t pixel = icons[r+offset][c] == WHITE ? OPTION == 0 ? GUI.hl : GUI.fg : GUI.bg;
        buffer[i++] = pixel; buffer[i++] = pixel;
      }}}
      ili9341_write_frame_rectangleLE(x, y+11, w, h, buffer);
      draw_text(x+20,y,(char *)"Resume",false,OPTION == 0?true:false, false);
      // restart
      i = 0;
      y+=42;
      offset = 10;
      for(int r = 0; r < 5; r++){for(int sr = 0; sr < 2; sr++){for(int c = 0; c < 5; c++) {
        uint16_t pixel = icons[r+offset][c] == WHITE ? OPTION == 1 ? GUI.hl : GUI.fg : GUI.bg;
        buffer[i++] = pixel; buffer[i++] = pixel;
      }}}
      ili9341_write_frame_rectangleLE(x, y+11, w, h, buffer);
      draw_text(x+20,y,(char *)"Restart",false,OPTION == 1?true:false, false);
      // delete
      i = 0;
      y+=42;
      offset = 20;
      for(int r = 0; r < 5; r++){for(int sr = 0; sr < 2; sr++){for(int c = 0; c < 5; c++) {
        uint16_t pixel = icons[r+offset][c] == WHITE ? OPTION == 2 ? GUI.hl : GUI.fg : GUI.bg;
        buffer[i++] = pixel; buffer[i++] = pixel;
      }}}
      ili9341_write_frame_rectangleLE(x, y+11, w, h, buffer);
      draw_text(x+20,y,(char *)"Delete Save",false,OPTION == 2?true:false, false);
    } else {
      // run
      i = 0;
      offset = 0;
      for(int r = 0; r < 5; r++){for(int sr = 0; sr < 2; sr++){for(int c = 0; c < 5; c++) {
        uint16_t pixel = icons[r+offset][c] == WHITE ? OPTION == 0 ? GUI.hl : GUI.fg : GUI.bg;
        buffer[i++] = pixel; buffer[i++] = pixel;
      }}}
      ili9341_write_frame_rectangleLE(x, y+11, w, h, buffer);
      draw_text(x+20,y,(char *)"Run",false,OPTION == 0?true:false, false);
    }

    // favorites
    y+=42;
    i = 0;
    offset = ROM.favorite?40:35;
    int option = SAVED ? 3 : 1;
    draw_mask(x,y-1,400,38);
    for(int r = 0; r < 5; r++){for(int sr = 0; sr < 2; sr++){for(int c = 0; c < 5; c++) {
      uint16_t pixel = icons[r+offset][c] == WHITE ? OPTION == option ? GUI.hl : GUI.fg : GUI.bg;
      buffer[i++] = pixel; buffer[i++] = pixel;
    }}}
    ili9341_write_frame_rectangleLE(x, y+11, w, h, buffer);
    draw_text(x+20,y,ROM.favorite?(char *)"Unfavorite":(char *)"Favorite",false,OPTION == option?true:false, false);
    display_flush();
  }
//}#pragma endregion GUI

//{#pragma region Files
  //{#pragma region Sort
    inline static void swap(char** a, char** b) {
        char* t = *a;
        *a = *b;
        *b = t;
    }

    static int strcicmp(char const *a, char const *b) {
        for (;; a++, b++)
        {
            int d = tolower((int)*a) - tolower((int)*b);
            if (d != 0 || !*a) return d;
        }
    }

    static int partition (char** arr, int low, int high) {
        char* pivot = arr[high];
        int i = (low - 1);

        for (int j = low; j <= high- 1; j++)
        {
            if (strcicmp(arr[j], pivot) < 0)
            {
                i++;
                swap(&arr[i], &arr[j]);
            }
        }
        swap(&arr[i + 1], &arr[high]);
        return (i + 1);
    }

    void quick_sort(char** arr, int low, int high) {
        if (low < high)
        {
            int pi = partition(arr, low, high);

            quick_sort(arr, low, pi - 1);
            quick_sort(arr, pi + 1, high);
        }
    }

    void sort_files(char** files)
    {
        if (ROMS.total > 1)
        {
            quick_sort(files, 0, ROMS.total - 1);
        }
    }
  //}#pragma endregion Sort

  void free_sorted_files() {
    if (SORTED_FILES) {
      for (int i = 0; i < SORTED_COUNT; i++) {
        if (SORTED_FILES[i]) free(SORTED_FILES[i]);
      }
      free(SORTED_FILES);
      SORTED_FILES = NULL;
      SORTED_COUNT = 0;
    }
  }

  void count_files() {
    delete_numbers();
    SEEK[0] = 0;

    ROMS.total = 0;
    char message[100];
    sprintf(message, "searching %s roms", DIRECTORIES[STEP]);
    int center = (WIDTH - strlen(message) * 20) / 2;
    draw_text(center,268,message,false,false, false);

    char path[256] = "/sd/roms/";
    strcat(&path[strlen(path) - 1], DIRECTORIES[STEP]);
    strcat(&path[strlen(path) - 1],folder_path);
    strcpy(ROM.path, path);

    if(directory != NULL) {
      closedir(directory);
      directory = NULL;
    }

    /* Free previous sorted list */
    free_sorted_files();

    directory = opendir(path);
    if(!directory) {
      draw_mask(0, 264, WIDTH,36);
      sprintf(message, "unable to open %s directory", DIRECTORIES[STEP]);
      int center = (WIDTH - strlen(message) * 20) / 2;
      draw_text(center,268,message,false,false, false);
      return;
    } else {
      if(directory == NULL) {
        draw_mask(0, 264, WIDTH,36);
        sprintf(message, "%s directory not found", DIRECTORIES[STEP]);
        int center = (WIDTH - strlen(message) * 20) / 2;
        draw_text(center,268,message,false,false, false);
      } else {
        /* First pass: count files */
        rewinddir(directory);
        seekdir(directory, 0);
        SEEK[0] = 0;
        struct dirent *file;
        while ((file = readdir(directory)) != NULL) {
          bool extenstion = matches_rom_extension(file->d_name, STEP);
          /* Neo Geo (STEP 17): skip directories and system files */
          bool is_dir = (file->d_type == 2);
          if (is_dir && STEP == 17) continue;
          if(extenstion || is_dir) {
            SEEK[ROMS.total+1] = telldir(directory);
            ROMS.total++;
          }
        }

        /* Second pass: read all filenames into SORTED_FILES */
        if (ROMS.total > 0) {
          int ext_length = strlen(EXTENSIONS[STEP]);
          SORTED_FILES = (char**)malloc(ROMS.total * sizeof(char*));
          SORTED_COUNT = 0;
          rewinddir(directory);
          seekdir(directory, 0);
          while ((file = readdir(directory)) != NULL && SORTED_COUNT < ROMS.total) {
            bool extenstion = matches_rom_extension(file->d_name, STEP);
            bool is_dir2 = (file->d_type == 2);
            if (is_dir2 && STEP == 17) continue;
            if(extenstion || is_dir2) {
              size_t len = strlen(file->d_name);
              if (file->d_type == 2) {
                SORTED_FILES[SORTED_COUNT] = (char*)malloc(len + 5);
                char dir[256];
                strcpy(dir, file->d_name);
                char dd[8];
                sprintf(dd, "%s", ext_length == 2 ? "dir" : ".dir");
                strcat(&dir[strlen(dir) - 1], dd);
                strcpy(SORTED_FILES[SORTED_COUNT], dir);
              } else {
                SORTED_FILES[SORTED_COUNT] = (char*)malloc(len + 1);
                strcpy(SORTED_FILES[SORTED_COUNT], file->d_name);
              }
              SORTED_COUNT++;
            }
          }

          /* Sort alphabetically */
          if (SORTED_COUNT > 1) {
            quick_sort(SORTED_FILES, 0, SORTED_COUNT - 1);
          }

          /* Move last-played ROM to the front if found */
          char *last_path = odroid_settings_RomFilePath_get();
          if (last_path && strlen(last_path) > 0) {
            /* Extract just the filename from the full path */
            char *last_name = last_path;
            for (char *p = last_path; *p; p++) {
              if (*p == '/') last_name = p + 1;
            }
            /* Check if this ROM belongs to the current system directory */
            char sys_prefix[256] = "/sd/roms/";
            strcat(&sys_prefix[strlen(sys_prefix) - 1], DIRECTORIES[STEP]);
            strcat(&sys_prefix[strlen(sys_prefix) - 1], "/");
            bool same_system = (strncmp(last_path, sys_prefix, strlen(sys_prefix)) == 0);
            /* Also check with folder_path for subfolder match */
            if (!same_system && folder_path[0] != 0) {
              char sub_prefix[256] = "/sd/roms/";
              strcat(&sub_prefix[strlen(sub_prefix) - 1], DIRECTORIES[STEP]);
              strcat(&sub_prefix[strlen(sub_prefix) - 1], folder_path);
              strcat(&sub_prefix[strlen(sub_prefix) - 1], "/");
              same_system = (strncmp(last_path, sub_prefix, strlen(sub_prefix)) == 0);
            }
            if (same_system && strlen(last_name) > 0) {
              for (int i = 0; i < SORTED_COUNT; i++) {
                if (strcicmp(SORTED_FILES[i], last_name) == 0) {
                  /* Insert a duplicate at front; keep original in place */
                  SORTED_FILES = (char**)realloc(SORTED_FILES, (SORTED_COUNT + 1) * sizeof(char*));
                  size_t ln = strlen(SORTED_FILES[i]) + 1;
                  char *dup = (char*)malloc(ln);
                  memcpy(dup, SORTED_FILES[i], ln);
                  /* Shift ALL elements right by one so original stays */
                  for (int j = SORTED_COUNT; j > 0; j--) {
                    SORTED_FILES[j] = SORTED_FILES[j - 1];
                  }
                  SORTED_FILES[0] = dup;
                  SORTED_COUNT++;
                  break;
                }
              }
            }
            free(last_path);
          }

          ROMS.total = SORTED_COUNT;
        }
      }
    }
  }

  void seek_files() {
    delete_numbers();

    char message[100];

    char path[256] = "/sd/roms/";
    strcat(&path[strlen(path) - 1], DIRECTORIES[STEP]);
    strcat(&path[strlen(path) - 1],folder_path);
    strcpy(ROM.path, path);

    free(FILES);
    FILES = (char**)malloc(ROMS.limit * sizeof(void*));

    if (!SORTED_FILES || SORTED_COUNT == 0) {
      draw_mask(0, 264, WIDTH,36);
      sprintf(message, "no %s roms available", DIRECTORIES[STEP]);
      int center = (WIDTH - strlen(message) * 20) / 2;
      draw_text(center,268,message,false,false, false);
      return;
    }

    /* Copy a page of filenames from SORTED_FILES into FILES */
    ROMS.pages = ROMS.total/ROMS.limit;
    if(ROMS.offset > ROMS.total) { ROMS.offset = 0;}
    int limit = (ROMS.total - ROMS.offset) < ROMS.limit ?
      (ROMS.total - ROMS.offset) : ROMS.limit;
    for (int n = 0; n < limit; n++) {
      size_t len = strlen(SORTED_FILES[ROMS.offset + n]);
      FILES[n] = (char*)malloc(len + 1);
      strcpy(FILES[n], SORTED_FILES[ROMS.offset + n]);
    }

    if(ROMS.total != 0) {
      draw_files();
    } else {
      sprintf(message, "no %s roms available", DIRECTORIES[STEP]);
      int center = (WIDTH - strlen(message) * 20) / 2;
      draw_mask(0, POS.y + 94, WIDTH,36);
      draw_mask(0, 264, WIDTH,36);
      draw_text(center,268,message,false,false, false);
    }
  }

  void get_files() {
    delete_numbers();
    count_files();
    seek_files();
  }

  void draw_files() {
    //printf("\n----- %s -----", __func__);
    int x = ORIGIN.x;
    int y = POS.y + 96;
    ROMS.page = ROMS.offset/ROMS.limit;

    for (int i = 0; i < 5; i++) draw_mask(0, y+(i*80)-12, WIDTH, 80);
    //int limit = ROMS.total < ROMS.limit ? ROMS.total : ROMS.limit;
    int limit = (ROMS.total - ROMS.offset) <  ROMS.limit ?
      (ROMS.total - ROMS.offset) :
      ROMS.limit;

    //printf("\nlimit:%d", limit);
    for(int n = 0; n < limit; n++) {
      //printf("\n%d:%s", n, FILES[n]);
      draw_text(x+48,y,FILES[n],true,n == 0 ? true : false, false);
      bool directory = strcmp(&FILES[n][strlen(FILES[n]) - 3], "dir") == 0;
      directory ?
        draw_folder(x,y,n == 0 ? true : false) :
        draw_media(x,y,n == 0 ? true : false,-1);
      if(n == 0) {
        strcpy(ROM.name, FILES[n]);
        strcpy(ROM.art, remove_ext(ROM.name, '.', '/'));
        /* Extract file extension into ROM.ext */
        char *dot = strrchr(FILES[n], '.');
        if (dot && dot != FILES[n]) {
          strcpy(ROM.ext, dot + 1);
        } else {
          strcpy(ROM.ext, EXTENSIONS[STEP]);
        }
        ROM.ready = true;
      }
      y+=44;
    }
    draw_numbers();
    //printf("\n---------------------\n");
  }

  void has_save_file(char *save_name) {
    SAVED = false;

    char save_dir[256] = "/sd/odroid/data/";
    strcat(&save_dir[strlen(save_dir) - 1], get_save_subdir());

    char save_file[256] = "";
    sprintf(save_file, "%s/%s", save_dir, save_name);
    strcat(&save_file[strlen(save_file) - 1], ".sav");

    struct stat st;
    if (stat(save_file, &st) == 0 && st.st_size > 0) {
      SAVED = true;
    }
  }
//}#pragma endregion Files

//{#pragma region Favorites
  void create_favorites() {
    //  printf("\n----- %s START -----", __func__);
    char file[256] = "/sd/odroid/data";
    sprintf(file, "%s/%s", file, RETROESP_FOLDER);
    sprintf(file, "%s/%s", file, FAVORITE_FILE);

    //struct stat st; if (stat(file, &st) == 0) {unlink(file);}

    FILE *f;
    f = fopen(file, "rb");
    if(f == NULL) {
      f = fopen(file, "w+");
    //  printf("\nCREATING: %s", file);
    } else {
      read_favorites();
    }
    //  printf("\nCLOSING: %s", file);
    fclose(f);
    //  printf("\n----- %s END -----\n", __func__);
  }

  void read_favorites() {
    //  printf("\n----- %s START -----", __func__);

    int n = 0;
    ROMS.total = 0;

    free(FAVORITES);
    FAVORITES = (char**)malloc((MAX_FILES_LIST) * sizeof(void*));

    char file[256] = "/sd/odroid/data";
    sprintf(file, "%s/%s", file, RETROESP_FOLDER);
    sprintf(file, "%s/%s", file, FAVORITE_FILE);

    FILE *f;
    f = fopen(file, "rb");
    if(f) {
    //  printf("\nREADING: %s\n", file);
      char line[256];
      while (fgets(line, sizeof(line), f)) {
        char *ep = &line[strlen(line)-1];
        while (*ep == '\n' || *ep == '\r'){*ep-- = '\0';}
      //  printf("\n%s", line);
        size_t len = strlen(line);
        FAVORITES[n] = (char*)malloc(len + 1);
        strcpy(FAVORITES[n], line);
        n++;
        ROMS.total++;
      }
    }
    fclose(f);

    // printf("\nROMS.total:%d\n", ROMS.total);
    char** TEMP = (char**)malloc((ROMS.total+1) * sizeof(void*));
    for(int n = ROMS.total-1; n >= 0; n--) {
      int i = (ROMS.total-1-n);
      size_t len = strlen(FAVORITES[n]);                                               
      TEMP[i] = (char*)malloc(len + 1);
      strcpy(TEMP[i], FAVORITES[n]);
    } 

    free(FAVORITES);
    FAVORITES = (char**)malloc((MAX_FILES_LIST) * sizeof(void*));

    for(int n = 0; n < ROMS.total; n++) {
      size_t len = strlen(TEMP[n]);                                               
      FAVORITES[n] = (char*)malloc(len + 1);
      strcpy(FAVORITES[n], TEMP[n]);
    } 

    free(TEMP);    

    //  printf("\n----- %s END -----\n", __func__);
  }

  void get_favorites() {
    //  printf("\n----- %s START -----", __func__);
    char message[100];
    sprintf(message, "loading favorites");
    int center = ceil((WIDTH/2)-((strlen(message)*20)/2));
    draw_text(center,268,message,false,false, false);

    read_favorites();
    process_favorites();

    //  printf("\n----- %s END -----", __func__);
  }

  void process_favorites() {
    //  printf("\n----- %s START -----", __func__);

    char message[100];

    ROMS.pages = ROMS.total/ROMS.limit;
    if(ROMS.offset > ROMS.total) { ROMS.offset = 0;}
    draw_browser_header();
    if(ROMS.total != 0) {
      draw_favorites();
    } else {
      sprintf(message, "no favorites available");
      int center = ceil((WIDTH/2)-((strlen(message)*20)/2));
      draw_mask(0, 40, WIDTH, 36);
      draw_mask(0, 240, WIDTH, 36);
      draw_text(center, 240, message, false, false, false);
    }

    display_flush();
    //  printf("\n----- %s END -----", __func__);
  }

  void draw_favorites() {
    //  printf("\n----- %s START -----", __func__);
    int x = 16;
    int y_start = 44;
    int row_h = 44;

    /* Clear all browser rows */
    for (int s = 0; s < BROWSER_LIMIT; s++) {
      draw_mask(0, y_start + s * row_h, WIDTH, row_h);
    }
    draw_mask(0, y_start + BROWSER_LIMIT * row_h, WIDTH, 8);

    int limit = (ROMS.total - ROMS.offset) < ROMS.limit ?
      (ROMS.total - ROMS.offset) : ROMS.limit;

    for(int n = ROMS.offset; n < (ROMS.offset+limit); n++) {
      int row = n - ROMS.offset;
      int y = y_start + row * row_h;
      char full[512];
      char trimmed[512];
      char favorite[256];
      char extension[10];
      char path[256];

      strcpy(full, FAVORITES[n]);
      strcpy(trimmed, remove_ext(full, '.', '/'));
      strcpy(favorite, get_filename(trimmed));
      strcpy(extension, get_ext(full));

      int length = (strlen(trimmed) - strlen(favorite)) - 1;
      memset(path, '\0', 256);
      strncpy(path, full, length);

      int offset = -1;
      if(ext_eq(extension, "nes")) {offset = 0*16;}
      if(ext_eq(extension, "gb")) {offset = 1*16;}
      if(ext_eq(extension, "gbc")) {offset = 2*16;}
      if(ext_eq(extension, "sms")) {offset = 3*16;}
      if(ext_eq(extension, "gg")) {offset = 4*16;}
      if(ext_eq(extension, "col")) {offset = 5*16;}
      if(ext_eq(extension, "z80")) {offset = 6*16;}
      if(ext_eq(extension, "a26")) {offset = 7*16;}
      if(ext_eq(extension, "a78")) {offset = 8*16;}
      if(ext_eq(extension, "lnx")) {offset = 9*16;}
      if(ext_eq(extension, "pce")) {offset = 10*16;}
      if(ext_eq(extension, "xex")) {offset = 7*16;}
      if(ext_eq(extension, "atr")) {offset = 7*16;}

      draw_text(x+40, y+6, favorite, false, row == BROWSER_SEL, false);
      draw_media(x, y, row == BROWSER_SEL, offset);
      if(row == BROWSER_SEL) {
        sprintf(favorite, "%s.%s", favorite, extension);
        strcpy(ROM.name, favorite);
        strcpy(ROM.art, remove_ext(ROM.name, '.', '/'));
        strcpy(ROM.path, path);
        strcpy(ROM.ext, extension);
        ROM.ready = true;
      }
    }

    /* Draw scrollbar */
    if (ROMS.total > BROWSER_LIMIT) {
      int bar_x = 788;
      int bar_h = BROWSER_LIMIT * row_h;
      int thumb_h = (BROWSER_LIMIT * bar_h) / ROMS.total;
      if (thumb_h < 8) thumb_h = 8;
      int thumb_y = y_start + (ROMS.offset * (bar_h - thumb_h)) / (ROMS.total - 1);
      draw_mask(bar_x, y_start, 8, bar_h);
      for (int i = 0; i < thumb_h * 8; i++) buffer[i] = GUI.fg;
      ili9341_write_frame_rectangleLE(bar_x, thumb_y, 8, thumb_h, buffer);
    }

    /*
    printf("\n\n***********"
      "\nROM details"
      "\n- ROM.name ->\t%s"
      "\n- ROM.art ->\t%s"
      "\n- ROM.path ->\t%s"
      "\n- ROM.ext ->\t%s"
      "\n- ROM.ready ->\t%d"
      "\n***********\n\n",
      ROM.name, ROM.art, ROM.path, ROM.ext, ROM.ready);
    */
    // printf("\n----- %s END -----", __func__);
  }

  /*
   * draw_favrecent_row - Redraw a single row in the fav/recent browser.
   * row  = visible row index (0..BROWSER_LIMIT-1)
   * list = FAVORITES or RECENTS array
   */
  void draw_favrecent_row(int row, char **list) {
    int x = 16;
    int y_start = 44;
    int row_h = 44;

    int limit = (ROMS.total - ROMS.offset) < BROWSER_LIMIT ?
      (ROMS.total - ROMS.offset) : BROWSER_LIMIT;
    if (row < 0 || row >= limit) return;

    int n = ROMS.offset + row;
    int y = y_start + row * row_h;
    bool selected = (row == BROWSER_SEL);

    draw_mask(0, y, WIDTH, row_h);

    char full[512];
    char trimmed[512];
    char favorite[256];
    char extension[10];
    char path[256];

    strcpy(full, list[n]);
    strcpy(trimmed, remove_ext(full, '.', '/'));
    strcpy(favorite, get_filename(trimmed));
    strcpy(extension, get_ext(full));

    int length = (strlen(trimmed) - strlen(favorite)) - 1;
    memset(path, '\0', 256);
    strncpy(path, full, length);

    int offset = -1;
    if(ext_eq(extension, "nes")) {offset = 0*16;}
    if(ext_eq(extension, "gb")) {offset = 1*16;}
    if(ext_eq(extension, "gbc")) {offset = 2*16;}
    if(ext_eq(extension, "sms")) {offset = 3*16;}
    if(ext_eq(extension, "gg")) {offset = 4*16;}
    if(ext_eq(extension, "col")) {offset = 5*16;}
    if(ext_eq(extension, "z80")) {offset = 6*16;}
    if(ext_eq(extension, "a26")) {offset = 7*16;}
    if(ext_eq(extension, "a78")) {offset = 8*16;}
    if(ext_eq(extension, "lnx")) {offset = 9*16;}
    if(ext_eq(extension, "pce")) {offset = 10*16;}
    if(ext_eq(extension, "xex")) {offset = 7*16;}
    if(ext_eq(extension, "atr")) {offset = 7*16;}

    draw_text(x+40, y+6, favorite, false, selected, false);
    draw_media(x, y, selected, offset);

    if(selected) {
      sprintf(favorite, "%s.%s", favorite, extension);
      strcpy(ROM.name, favorite);
      strcpy(ROM.art, remove_ext(ROM.name, '.', '/'));
      strcpy(ROM.path, path);
      strcpy(ROM.ext, extension);
      ROM.ready = true;
    }
  }

  /*
   * favrecent_partial_update - Redraw only old/new rows + header
   * for favorites or recents browser.
   */
  void favrecent_partial_update(int oldSel, int newSel, char **list) {
    draw_favrecent_row(oldSel, list);
    draw_favrecent_row(newSel, list);
    draw_browser_header();
    display_flush();
  }

  void add_favorite(char *favorite) {
    //  printf("\n----- %s START -----", __func__);
    char file[256] = "/sd/odroid/data";
    sprintf(file, "%s/%s", file, RETROESP_FOLDER);
    sprintf(file, "%s/%s", file, FAVORITE_FILE);
    FILE *f;
    f = fopen(file, "a+");
    if(f) {
    //  printf("\nADDING: %s to %s", favorite, file);
      fprintf(f, "%s\n", favorite);
    }
    fclose(f);
    //  printf("\n----- %s END -----\n", __func__);
  }

  void delete_favorite(char *favorite) {
    //  printf("\n----- %s START -----", __func__);

    int n = 0;
    int count = 0;

    free(FAVORITES);
    FAVORITES = (char**)malloc(50 * sizeof(void*));

    char file[256] = "/sd/odroid/data";
    sprintf(file, "%s/%s", file, RETROESP_FOLDER);
    sprintf(file, "%s/%s", file, FAVORITE_FILE);

    FILE *f;
    f = fopen(file, "rb");
    if(f) {
    //  printf("\nCHECKING: %s\n", favorite);
      char line[256];
      while (fgets(line, sizeof(line), f)) {
        char *ep = &line[strlen(line)-1];
        while (*ep == '\n' || *ep == '\r'){*ep-- = '\0';}
        if(strcmp(favorite, line) != 0) {
          size_t len = strlen(line);
          FAVORITES[n] = (char*)malloc(len + 1);
          strcpy(FAVORITES[n], line);
          n++;
          count++;
        }
      }
    }
    fclose(f);
    struct stat st;
    if (stat(file, &st) == 0) {
      unlink(file);
      create_favorites();
      for(n = 0; n < count; n++) {
        size_t len = strlen(FAVORITES[n]);
        if(len > 0) {
          add_favorite(FAVORITES[n]);
        //  printf("\n%s - %d" , FAVORITES[n], len);
        }
      }
    } else {
    //  printf("\nUNABLE TO UNLINK\n");
    }

    //  printf("\n----- %s END -----\n", __func__);
  }

  void is_favorite(char *favorite) {
    //  printf("\n----- %s START -----", __func__);
    ROM.favorite = false;

    char file[256] = "/sd/odroid/data";
    sprintf(file, "%s/%s", file, RETROESP_FOLDER);
    sprintf(file, "%s/%s", file, FAVORITE_FILE);


    FILE *f;
    f = fopen(file, "rb");
    if(f) {
    //  printf("\nCHECKING: %s\n", favorite);
      char line[256];
      while (fgets(line, sizeof(line), f)) {
        char *ep = &line[strlen(line)-1];
        while (*ep == '\n' || *ep == '\r'){*ep-- = '\0';}
        if(strcmp(favorite, line) == 0) {
          ROM.favorite = true;
        }
      }
    }
    fclose(f);
    //  printf("\n----- %s END -----\n", __func__);
  }

  /* Load all favorites into fav_cache[] for fast lookup by the browser */
  void load_favorites_cache(void) {
    /* Free old cache */
    for (int i = 0; i < fav_cache_count; i++) {
      free(fav_cache[i]);
      fav_cache[i] = NULL;
    }
    fav_cache_count = 0;

    char file[256];
    sprintf(file, "/sd/odroid/data/%s/%s", RETROESP_FOLDER, FAVORITE_FILE);
    FILE *f = fopen(file, "rb");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f) && fav_cache_count < FAV_CACHE_MAX) {
      /* Trim trailing newline/carriage return */
      char *ep = &line[strlen(line) - 1];
      while (ep >= line && (*ep == '\n' || *ep == '\r')) { *ep-- = '\0'; }
      if (line[0] == '\0') continue;
      fav_cache[fav_cache_count] = strdup(line);
      fav_cache_count++;
    }
    fclose(f);
  }

  /* Check if a full path (e.g. "/sd/roms/nes/mario.nes") is in the cache */
  bool check_favorite_cached(const char *full_path) {
    for (int i = 0; i < fav_cache_count; i++) {
      if (strcmp(fav_cache[i], full_path) == 0) return true;
    }
    return false;
  }

  /* Draw a yellow star icon at (x, y), size 16x16 scaled 2x to 32x32 */
  static const uint8_t star_16x16[16][16] = {
    {0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,1,1,0,0,0,0,0,0},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0},
    {0,0,1,1,1,1,1,1,1,1,1,1,1,1,0,0},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0},
    {0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0},
    {0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0},
    {0,0,1,1,1,1,0,0,0,0,1,1,1,1,0,0},
    {0,0,1,1,1,0,0,0,0,0,0,1,1,1,0,0},
    {0,1,1,1,0,0,0,0,0,0,0,0,1,1,1,0},
    {0,1,1,0,0,0,0,0,0,0,0,0,0,1,1,0},
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
  };

  void draw_star(int x, int y) {
    uint16_t yellow = 0xFFE0;  /* RGB565 yellow */
    int i = 0;
    for (int h = 0; h < 16; h++) {
      for (int sh = 0; sh < 2; sh++) {
        for (int w = 0; w < 16; w++) {
          uint16_t pixel = star_16x16[h][w] ? yellow : GUI.bg;
          buffer[i++] = pixel;
          buffer[i++] = pixel;
        }
      }
    }
    ili9341_write_frame_rectangleLE(x, y, 32, 32, buffer);
  }

//}#pragma endregion Favorites

//{#pragma region Search
#ifndef CONFIG_HDMI_OUTPUT
  /*
   * Touch-based visual keyboard search for the ROM browser.
   * X button toggles open/closed. Touch keys to type.
   * The file list scrolls to the first match as you type.
   * A confirms (jumps to match and closes). B/X close.
   *
   * Keyboard layout (4 rows, bottom of screen):
   *   Row 0: Q W E R T Y U I O P
   *   Row 1: A S D F G H J K L
   *   Row 2: Z X C V B N M
   *   Row 3: [SPACE] [DEL] [CLOSE]
   *
   * Touch coordinates from GT911 are portrait 480x800.
   * UI is 800x480 (270 deg CCW rotation).
   * Mapping: ui_x = touch_y,  ui_y = 479 - touch_x
   */

  #define KB_ROWS      4
  #define KB_KEY_W     68   /* key width in UI pixels  */
  #define KB_KEY_H     42   /* key height in UI pixels */
  #define KB_GAP       4    /* gap between keys        */
  #define KB_Y_START   290  /* Y offset for top of keyboard area */
  #define KB_X_PAD     16   /* left padding */
  #define SEARCH_MAX   20   /* max search string length */

  static const char *kb_rows[KB_ROWS] = {
    "QWERTYUIOP",
    "ASDFGHJKL",
    "ZXCVBNM",
    NULL  /* special row: SPACE, DEL, CLOSE */
  };
  static const int kb_row_len[KB_ROWS] = { 10, 9, 7, 3 };

  /* Draw a single key */
  static void kb_draw_key(int kx, int ky, int kw, int kh,
                           const char *label, bool highlight) {
    uint16_t bg = highlight ? 0x4208 : 0x2104;  /* lighter/darker gray */
    uint16_t fg = 0xFFFF;  /* white text */
    uint16_t border = 0x6B4D;  /* light gray border */

    /* Fill background strip by strip (buffer is 64000 shorts max) */
    int total = kw * kh;
    if (total > 64000) total = 64000;
    for (int i = 0; i < total; i++) buffer[i] = bg;
    /* Top border */
    for (int i = 0; i < kw; i++) buffer[i] = border;
    /* Bottom border */
    for (int i = (kh - 1) * kw; i < kh * kw; i++) buffer[i] = border;
    /* Left/right border */
    for (int r = 0; r < kh; r++) {
      buffer[r * kw] = border;
      buffer[r * kw + kw - 1] = border;
    }
    ili9341_write_frame_rectangleLE(kx, ky, kw, kh, buffer);

    /* Draw label centered in key */
    int len = strlen(label);
    int tx = kx + (kw - len * 16) / 2;  /* 16px per char at 2x */
    int ty = ky + (kh - 32) / 2;
    /* Use normal (non-ext) draw: one char at a time */
    for (int c = 0; c < len; c++) {
      char ch = label[c];
      if (ch == ' ') { tx += 16; continue; }
      int glyph = (ch >= 32 && ch <= 126) ? ch - 32 : '?' - 32;
      int idx = 0;
      for (int row = 0; row < 16; row++) {
        uint8_t bits = FONT_8x16[glyph][row];
        for (int sr = 0; sr < 2; sr++) {
          for (int col = 7; col >= 0; col--) {
            buffer[idx++] = (bits & (1 << col)) ? fg : bg;
            buffer[idx++] = (bits & (1 << col)) ? fg : bg;
          }
        }
      }
      ili9341_write_frame_rectangleLE(tx, ty, 16, 32, buffer);
      tx += 16;
    }
  }

  /* Draw the full keyboard overlay */
  static void kb_draw(const char *search_str) {
    /* Search bar at top of keyboard area */
    int bar_y = KB_Y_START - 44;
    draw_mask(0, bar_y, WIDTH, 44);

    /* Draw "SEARCH:" label + current text */
    char display_str[SEARCH_MAX + 12];
    snprintf(display_str, sizeof(display_str), "SEARCH: %s_", search_str);
    draw_text(KB_X_PAD, bar_y + 6, display_str, false, true, false);

    /* Clear keyboard area */
    draw_mask(0, KB_Y_START, WIDTH, KB_ROWS * (KB_KEY_H + KB_GAP) + KB_GAP);

    /* Draw letter rows */
    for (int r = 0; r < 3; r++) {
      const char *row = kb_rows[r];
      int row_len = kb_row_len[r];
      int row_width = row_len * (KB_KEY_W + KB_GAP) - KB_GAP;
      int x_start = (WIDTH - row_width) / 2;
      int ky = KB_Y_START + r * (KB_KEY_H + KB_GAP);

      for (int k = 0; k < row_len; k++) {
        int kx = x_start + k * (KB_KEY_W + KB_GAP);
        char label[2] = { row[k], '\0' };
        kb_draw_key(kx, ky, KB_KEY_W, KB_KEY_H, label, false);
      }
    }

    /* Special row 3: SPACE, DEL, CLOSE */
    int r3_y = KB_Y_START + 3 * (KB_KEY_H + KB_GAP);
    int space_w = 200, del_w = 120, close_w = 120;
    int total_w = space_w + KB_GAP + del_w + KB_GAP + close_w;
    int r3_x = (WIDTH - total_w) / 2;
    kb_draw_key(r3_x, r3_y, space_w, KB_KEY_H, "SPACE", false);
    kb_draw_key(r3_x + space_w + KB_GAP, r3_y, del_w, KB_KEY_H, "DEL", false);
    kb_draw_key(r3_x + space_w + KB_GAP + del_w + KB_GAP, r3_y, close_w, KB_KEY_H, "SEARCH", false);
  }

  /* Given a touch point in UI coordinates, return the pressed key char.
   * Returns '\b' for DEL, ' ' for SPACE, '\x1b' for CLOSE, '\0' for no hit. */
  static char kb_hit_test(int ui_x, int ui_y) {
    /* Letter rows */
    for (int r = 0; r < 3; r++) {
      int row_len = kb_row_len[r];
      int row_width = row_len * (KB_KEY_W + KB_GAP) - KB_GAP;
      int x_start = (WIDTH - row_width) / 2;
      int ky = KB_Y_START + r * (KB_KEY_H + KB_GAP);

      if (ui_y >= ky && ui_y < ky + KB_KEY_H) {
        for (int k = 0; k < row_len; k++) {
          int kx = x_start + k * (KB_KEY_W + KB_GAP);
          if (ui_x >= kx && ui_x < kx + KB_KEY_W) {
            return kb_rows[r][k];
          }
        }
      }
    }

    /* Special row */
    int r3_y = KB_Y_START + 3 * (KB_KEY_H + KB_GAP);
    if (ui_y >= r3_y && ui_y < r3_y + KB_KEY_H) {
      int space_w = 200, del_w = 120, close_w = 120;
      int total_w = space_w + KB_GAP + del_w + KB_GAP + close_w;
      int r3_x = (WIDTH - total_w) / 2;
      if (ui_x >= r3_x && ui_x < r3_x + space_w) return ' ';
      if (ui_x >= r3_x + space_w + KB_GAP && ui_x < r3_x + space_w + KB_GAP + del_w) return '\b';
      if (ui_x >= r3_x + space_w + KB_GAP + del_w + KB_GAP &&
          ui_x < r3_x + space_w + KB_GAP + del_w + KB_GAP + close_w) return '\x1b';
    }

    return '\0';
  }

  /* Search: find the first SORTED_FILES entry matching prefix (case-insensitive).
   * First tries prefix match, then falls back to substring (contains) match.
   * Returns the index in SORTED_FILES or -1.  */
  static int search_find_match(const char *prefix) {
    if (!SORTED_FILES || SORTED_COUNT == 0 || prefix[0] == '\0') return -1;
    int plen = strlen(prefix);
    /* Pass 1: prefix match */
    for (int i = 0; i < SORTED_COUNT; i++) {
      if (strncasecmp(SORTED_FILES[i], prefix, plen) == 0) return i;
    }
    /* Pass 2: substring match */
    for (int i = 0; i < SORTED_COUNT; i++) {
      const char *name = SORTED_FILES[i];
      int nlen = strlen(name);
      for (int j = 0; j <= nlen - plen; j++) {
        if (strncasecmp(&name[j], prefix, plen) == 0) return i;
      }
    }
    return -1;
  }

  /*
   * show_search_keyboard - Main search entry point.
   * Called when X is pressed in the browser (STEP >= 3).
   * Draws keyboard, handles touch, updates search filter.
   * On exit, ROMS.offset and BROWSER_SEL are set to the match position.
   */
  void show_search_keyboard(void) {
    char search_str[SEARCH_MAX + 1] = "";
    int search_len = 0;
    bool was_touched = false;

    /* Disable touch-to-button mapping so keyboard touches don't trigger MENU */
    odroid_input_touch_buttons_disable = true;

    /* Wait for all buttons release so we don't immediately exit */
    do { vTaskDelay(pdMS_TO_TICKS(30)); odroid_input_gamepad_read(&gamepad); }
    while (gamepad.values[ODROID_INPUT_MENU] || gamepad.values[ODROID_INPUT_A] ||
           gamepad.values[ODROID_INPUT_B] || gamepad.values[ODROID_INPUT_X]);

    /* Draw the keyboard */
    kb_draw(search_str);
    display_flush();

    while (true) {
      vTaskDelay(pdMS_TO_TICKS(30));

      /* Check physical gamepad for exit (B closes, A confirms). */
      odroid_input_gamepad_read(&gamepad);
      if (gamepad.values[ODROID_INPUT_B]) {
        debounce(ODROID_INPUT_B);
        break;
      }
      if (gamepad.values[ODROID_INPUT_A]) {
        debounce(ODROID_INPUT_A);
        break;
      }

      /* Read touch */
      uint16_t tx = 0, ty = 0;
      bool touched = gt911_touch_get_xy(&tx, &ty);

      /* Map portrait touch to landscape UI */
      int ui_x = (int)ty;
      int ui_y = 479 - (int)tx;

      if (touched && !was_touched) {
        /* New touch â€” check keyboard hit */
        char key = kb_hit_test(ui_x, ui_y);

        if (key == '\x1b') {
          break;
        } else if (key == '\b') {
          if (search_len > 0) {
            search_str[--search_len] = '\0';
          }
        } else if (key != '\0' && search_len < SEARCH_MAX) {
          search_str[search_len++] = key;
          search_str[search_len] = '\0';
        }

        if (key != '\0') {
          int match = search_find_match(search_str);
          if (match >= 0) {
            ROMS.offset = match;
            if (ROMS.offset + BROWSER_LIMIT > ROMS.total)
              ROMS.offset = ROMS.total > BROWSER_LIMIT ? ROMS.total - BROWSER_LIMIT : 0;
            BROWSER_SEL = match - ROMS.offset;
          }
          /* Redraw keyboard + search bar only (browser redraws on exit) */
          kb_draw(search_str);
          display_flush();
        }
      }

      was_touched = touched;
    }

    /* Re-enable touch-to-button mapping */
    odroid_input_touch_buttons_disable = false;

    /* Redraw browser (clear keyboard overlay) */
    if (ROMS.total > 0) {
      seek_files();
      draw_browser_screen();
    }
  }
#endif /* !CONFIG_HDMI_OUTPUT */

#ifdef CONFIG_HDMI_OUTPUT
  /*
   * Gamepad-driven visual keyboard search for the ROM browser (HDMI variant).
   * X button toggles open/closed. D-pad navigates, A selects key, B deletes.
   *
   * Keyboard layout (4 rows):
   *   Row 0: Q W E R T Y U I O P
   *   Row 1: A S D F G H J K L
   *   Row 2: Z X C V B N M
   *   Row 3: [SPACE] [DEL] [SEARCH]
   */

  #define KB_ROWS      4
  #define KB_KEY_W     54   /* key width  (fits 10 keys in 640px) */
  #define KB_KEY_H     38   /* key height */
  #define KB_GAP       4    /* gap between keys */
  #define KB_Y_START   290  /* Y offset for top of keyboard area */
  #define KB_X_PAD     16   /* left padding */
  #define SEARCH_MAX   20   /* max search string length */

  static const char *kb_rows[KB_ROWS] = {
    "QWERTYUIOP",
    "ASDFGHJKL",
    "ZXCVBNM",
    NULL  /* special row: SPACE, DEL, SEARCH */
  };
  static const int kb_row_len[KB_ROWS] = { 10, 9, 7, 3 };

  /* Draw a single key */
  static void kb_draw_key(int kx, int ky, int kw, int kh,
                           const char *label, bool highlight) {
    uint16_t bg = highlight ? 0x4208 : 0x2104;  /* lighter/darker gray */
    uint16_t fg = 0xFFFF;  /* white text */
    uint16_t border = highlight ? 0xFFE0 : 0x6B4D;  /* yellow highlight / gray */

    int total = kw * kh;
    if (total > 64000) total = 64000;
    for (int i = 0; i < total; i++) buffer[i] = bg;
    for (int i = 0; i < kw; i++) buffer[i] = border;
    for (int i = (kh - 1) * kw; i < kh * kw; i++) buffer[i] = border;
    for (int r = 0; r < kh; r++) {
      buffer[r * kw] = border;
      buffer[r * kw + kw - 1] = border;
    }
    ili9341_write_frame_rectangleLE(kx, ky, kw, kh, buffer);

    int len = strlen(label);
    int tx = kx + (kw - len * 16) / 2;
    int ty = ky + (kh - 32) / 2;
    for (int c = 0; c < len; c++) {
      char ch = label[c];
      if (ch == ' ') { tx += 16; continue; }
      int glyph = (ch >= 32 && ch <= 126) ? ch - 32 : '?' - 32;
      int idx = 0;
      for (int row = 0; row < 16; row++) {
        uint8_t bits = FONT_8x16[glyph][row];
        for (int sr = 0; sr < 2; sr++) {
          for (int col = 7; col >= 0; col--) {
            buffer[idx++] = (bits & (1 << col)) ? fg : bg;
            buffer[idx++] = (bits & (1 << col)) ? fg : bg;
          }
        }
      }
      ili9341_write_frame_rectangleLE(tx, ty, 16, 32, buffer);
      tx += 16;
    }
  }

  /* Draw the full keyboard overlay with cursor highlight */
  static void kb_draw_hdmi(const char *search_str, int cur_row, int cur_col) {
    /* Search bar */
    int bar_y = KB_Y_START - 44;
    draw_mask(0, bar_y, WIDTH, 44);
    char display_str[SEARCH_MAX + 12];
    snprintf(display_str, sizeof(display_str), "SEARCH: %s_", search_str);
    draw_text(KB_X_PAD, bar_y + 6, display_str, false, true, false);

    /* Clear keyboard area */
    draw_mask(0, KB_Y_START, WIDTH, KB_ROWS * (KB_KEY_H + KB_GAP) + KB_GAP);

    /* Draw letter rows */
    for (int r = 0; r < 3; r++) {
      const char *row = kb_rows[r];
      int row_len = kb_row_len[r];
      int row_width = row_len * (KB_KEY_W + KB_GAP) - KB_GAP;
      int x_start = (WIDTH - row_width) / 2;
      int ky = KB_Y_START + r * (KB_KEY_H + KB_GAP);

      for (int k = 0; k < row_len; k++) {
        int kx = x_start + k * (KB_KEY_W + KB_GAP);
        char label[2] = { row[k], '\0' };
        bool hl = (r == cur_row && k == cur_col);
        kb_draw_key(kx, ky, KB_KEY_W, KB_KEY_H, label, hl);
      }
    }

    /* Special row 3: SPACE, DEL, SEARCH */
    int r3_y = KB_Y_START + 3 * (KB_KEY_H + KB_GAP);
    int space_w = 160, del_w = 100, search_w = 100;
    int total_w = space_w + KB_GAP + del_w + KB_GAP + search_w;
    int r3_x = (WIDTH - total_w) / 2;
    kb_draw_key(r3_x, r3_y, space_w, KB_KEY_H, "SPACE",
                cur_row == 3 && cur_col == 0);
    kb_draw_key(r3_x + space_w + KB_GAP, r3_y, del_w, KB_KEY_H, "DEL",
                cur_row == 3 && cur_col == 1);
    kb_draw_key(r3_x + space_w + KB_GAP + del_w + KB_GAP, r3_y, search_w, KB_KEY_H, "SEARCH",
                cur_row == 3 && cur_col == 2);
  }

  /* Search: find the first SORTED_FILES entry matching prefix (case-insensitive). */
  static int search_find_match(const char *prefix) {
    if (!SORTED_FILES || SORTED_COUNT == 0 || prefix[0] == '\0') return -1;
    int plen = strlen(prefix);
    for (int i = 0; i < SORTED_COUNT; i++) {
      if (strncasecmp(SORTED_FILES[i], prefix, plen) == 0) return i;
    }
    for (int i = 0; i < SORTED_COUNT; i++) {
      const char *name = SORTED_FILES[i];
      int nlen = strlen(name);
      for (int j = 0; j <= nlen - plen; j++) {
        if (strncasecmp(&name[j], prefix, plen) == 0) return i;
      }
    }
    return -1;
  }

  void show_search_keyboard(void) {
    char search_str[SEARCH_MAX + 1] = "";
    int search_len = 0;
    int cur_row = 0, cur_col = 0;

    /* Wait for button release */
    do { vTaskDelay(pdMS_TO_TICKS(30)); odroid_input_gamepad_read(&gamepad); }
    while (gamepad.values[ODROID_INPUT_MENU] || gamepad.values[ODROID_INPUT_A] ||
           gamepad.values[ODROID_INPUT_B]);

    kb_draw_hdmi(search_str, cur_row, cur_col);
    display_flush();

    while (true) {
      vTaskDelay(pdMS_TO_TICKS(120));
      odroid_input_gamepad_read(&gamepad);

      bool redraw = false;

      /* Navigation */
      if (gamepad.values[ODROID_INPUT_UP]) {
        cur_row = (cur_row + KB_ROWS - 1) % KB_ROWS;
        if (cur_col >= kb_row_len[cur_row]) cur_col = kb_row_len[cur_row] - 1;
        redraw = true;
      }
      if (gamepad.values[ODROID_INPUT_DOWN]) {
        cur_row = (cur_row + 1) % KB_ROWS;
        if (cur_col >= kb_row_len[cur_row]) cur_col = kb_row_len[cur_row] - 1;
        redraw = true;
      }
      if (gamepad.values[ODROID_INPUT_LEFT]) {
        cur_col = (cur_col + kb_row_len[cur_row] - 1) % kb_row_len[cur_row];
        redraw = true;
      }
      if (gamepad.values[ODROID_INPUT_RIGHT]) {
        cur_col = (cur_col + 1) % kb_row_len[cur_row];
        redraw = true;
      }

      /* A = select key */
      if (gamepad.values[ODROID_INPUT_A]) {
        char key = '\0';
        if (cur_row < 3) {
          key = kb_rows[cur_row][cur_col];
        } else {
          if (cur_col == 0) key = ' ';       /* SPACE */
          else if (cur_col == 1) key = '\b';  /* DEL */
          else key = '\x1b';                  /* SEARCH (close) */
        }

        if (key == '\x1b') break;
        else if (key == '\b') {
          if (search_len > 0) search_str[--search_len] = '\0';
        } else if (key != '\0' && search_len < SEARCH_MAX) {
          search_str[search_len++] = key;
          search_str[search_len] = '\0';
        }

        /* Update match */
        int match = search_find_match(search_str);
        if (match >= 0) {
          ROMS.offset = match;
          if (ROMS.offset + BROWSER_LIMIT > ROMS.total)
            ROMS.offset = ROMS.total > BROWSER_LIMIT ? ROMS.total - BROWSER_LIMIT : 0;
          BROWSER_SEL = match - ROMS.offset;
        }
        redraw = true;
        vTaskDelay(pdMS_TO_TICKS(80));  /* brief delay after key press */
      }

      /* B = delete last char */
      if (gamepad.values[ODROID_INPUT_B]) {
        if (search_len > 0) {
          search_str[--search_len] = '\0';
          int match = search_find_match(search_str);
          if (match >= 0) {
            ROMS.offset = match;
            if (ROMS.offset + BROWSER_LIMIT > ROMS.total)
              ROMS.offset = ROMS.total > BROWSER_LIMIT ? ROMS.total - BROWSER_LIMIT : 0;
            BROWSER_SEL = match - ROMS.offset;
          }
          redraw = true;
        } else {
          /* Empty search + B = close */
          break;
        }
        vTaskDelay(pdMS_TO_TICKS(80));
      }

      /* START / Y = close (confirm search) */
      if (gamepad.values[ODROID_INPUT_START] || gamepad.values[ODROID_INPUT_Y]) break;

      if (redraw) {
        kb_draw_hdmi(search_str, cur_row, cur_col);
        display_flush();
      }
    }

    /* Redraw browser (clear keyboard overlay) */
    if (ROMS.total > 0) {
      seek_files();
      draw_browser_screen();
    }
  }
#endif /* CONFIG_HDMI_OUTPUT */

//}#pragma endregion Search

//{#pragma region Recents
  void create_recents() {
    //  printf("\n----- %s START -----", __func__);
    char file[256] = "/sd/odroid/data";
    sprintf(file, "%s/%s", file, RETROESP_FOLDER);
    sprintf(file, "%s/%s", file, RECENT_FILE);

    //struct stat st; if (stat(file, &st) == 0) {unlink(file);}

    FILE *f;
    f = fopen(file, "rb");
    if(f == NULL) {
      f = fopen(file, "w+");
    //  printf("\nCREATING: %s", file);
    } else {
      read_recents();
    }
    //  f = fopen(file, "w+");
    //  printf("\nCLOSING: %s", file);
    fclose(f);
    //  printf("\n----- %s END -----\n", __func__);
  }

  void read_recents() {
    // printf("\n----- %s START -----", __func__);

    int n = 0;
    ROMS.total = 0;

    free(RECENTS);
    RECENTS = (char**)malloc((MAX_FILES_LIST) * sizeof(void*));

    char file[256] = "/sd/odroid/data";
    sprintf(file, "%s/%s", file, RETROESP_FOLDER);
    sprintf(file, "%s/%s", file, RECENT_FILE);

    FILE *f;
    f = fopen(file, "rb");
    if(f) {
      //  printf("\nREADING: %s\n", file);
      char line[256];

      while (fgets(line, sizeof(line), f)) {
        char *ep = &line[strlen(line)-1];
        while (*ep == '\n' || *ep == '\r'){*ep-- = '\0';}
        size_t len = strlen(line); 
        RECENTS[n] = (char*)malloc(len + 1);
        strcpy(RECENTS[n], line);
        n++;
        ROMS.total++;
      }
    }
    fclose(f);

    // printf("\nROMS.total:%d\n", ROMS.total);
    char** TEMP = (char**)malloc((ROMS.total+1) * sizeof(void*));
    for(int n = ROMS.total-1; n >= 0; n--) {
      int i = (ROMS.total-1-n);
      size_t len = strlen(RECENTS[n]);                                               
      TEMP[i] = (char*)malloc(len + 1);
      strcpy(TEMP[i], RECENTS[n]);
    } 

    free(RECENTS);
    RECENTS = (char**)malloc((MAX_FILES_LIST) * sizeof(void*));

    for(int n = 0; n < ROMS.total; n++) {
      size_t len = strlen(TEMP[n]);                                               
      RECENTS[n] = (char*)malloc(len + 1);
      strcpy(RECENTS[n], TEMP[n]);
    } 

    free(TEMP);

    // printf("\n----- %s END -----\n", __func__);
  }

  void add_recent(char *recent) {
    //  printf("\n----- %s START -----", __func__);

    int n = 0;
    int count = 0;

    free(RECENTS);
    //RECENTS = (char**)malloc(50 * sizeof(void*));
    RECENTS = (char**)malloc((MAX_FILES_LIST) * sizeof(void*));
    char file[256] = "/sd/odroid/data";
    sprintf(file, "%s/%s", file, RETROESP_FOLDER);
    sprintf(file, "%s/%s", file, RECENT_FILE);

    FILE *f;
    f = fopen(file, "rb");
    if(f) {
      //  printf("\nCHECKING: %s\n", recent);
      char line[256];
      while (fgets(line, sizeof(line), f)) {
        char *ep = &line[strlen(line)-1];
        while (*ep == '\n' || *ep == '\r'){*ep-- = '\0';}
        if(strcmp(recent, line) != 0) {
          size_t len = strlen(line);
          RECENTS[n] = (char*)malloc(len + 1);
          strcpy(RECENTS[n], line);
          n++;
          count++;
        }
      }
    }
    fclose(f);

    struct stat st;
    if (stat(file, &st) == 0) {
      unlink(file);
      create_recents();

      f = fopen(file, "a+");
      if(f) {

      }
      for(n = 0; n < count; n++) {
        size_t len = strlen(RECENTS[n]);
        if(len > 0) {
          //printf("\n%s - %d" ,RECENTS[n], len);
          fprintf(f, "%s\n",RECENTS[n]);
        }
      }
      //  printf("\nADDING: %s\n", recent);                        
      fprintf(f, "%s\n", recent);
    } else {
    //  printf("\nUNABLE TO UNLINK\n");
    }

    fclose(f);

    //  printf("\n----- %s END -----\n", __func__); 
  }

  void delete_recent(char *recent) {

  }

  void get_recents() {
    //  printf("\n----- %s START -----", __func__);
    char message[100];
    sprintf(message, "loading recents");
    int center = ceil((WIDTH/2)-((strlen(message)*20)/2));
    draw_text(center,268,message,false,false, false);

    read_recents();
    process_recents();

    //  printf("\n----- %s END -----", __func__);
  }

  void process_recents() {
    //  printf("\n----- %s START -----", __func__);

    char message[100];

    ROMS.pages = ROMS.total/ROMS.limit;
    if(ROMS.offset > ROMS.total) { ROMS.offset = 0;}
    draw_browser_header();
    if(ROMS.total != 0) {
      draw_recents();
    } else {
      sprintf(message, "no recents available");
      int center = ceil((WIDTH/2)-((strlen(message)*20)/2));
      draw_mask(0, 40, WIDTH, 36);
      draw_mask(0, 240, WIDTH, 36);
      draw_text(center, 240, message, false, false, false);
    }

    display_flush();
    //  printf("\n----- %s END -----", __func__);
  }

  void draw_recents() {
    //  printf("\n----- %s START -----", __func__);
    int x = 16;
    int y_start = 44;
    int row_h = 44;

    /* Clear all browser rows */
    for (int s = 0; s < BROWSER_LIMIT; s++) {
      draw_mask(0, y_start + s * row_h, WIDTH, row_h);
    }
    draw_mask(0, y_start + BROWSER_LIMIT * row_h, WIDTH, 8);

    int limit = (ROMS.total - ROMS.offset) < ROMS.limit ?
      (ROMS.total - ROMS.offset) : ROMS.limit;

    for(int n = ROMS.offset; n < (ROMS.offset+limit); n++) {
      int row = n - ROMS.offset;
      int y = y_start + row * row_h;
      char full[512];
      char trimmed[512];
      char favorite[256];
      char extension[10];
      char path[256];

      strcpy(full, RECENTS[n]);
      strcpy(trimmed, remove_ext(full, '.', '/'));
      strcpy(favorite, get_filename(trimmed));
      strcpy(extension, get_ext(full));

      int length = (strlen(trimmed) - strlen(favorite)) - 1;
      memset(path, '\0', 256);
      strncpy(path, full, length);

      int offset = -1;
      if(ext_eq(extension, "nes")) {offset = 0*16;}
      if(ext_eq(extension, "gb")) {offset = 1*16;}
      if(ext_eq(extension, "gbc")) {offset = 2*16;}
      if(ext_eq(extension, "sms")) {offset = 3*16;}
      if(ext_eq(extension, "gg")) {offset = 4*16;}
      if(ext_eq(extension, "col")) {offset = 5*16;}
      if(ext_eq(extension, "z80")) {offset = 6*16;}
      if(ext_eq(extension, "a26")) {offset = 7*16;}
      if(ext_eq(extension, "a78")) {offset = 8*16;}
      if(ext_eq(extension, "lnx")) {offset = 9*16;}
      if(ext_eq(extension, "pce")) {offset = 10*16;}
      if(ext_eq(extension, "xex")) {offset = 7*16;}
      if(ext_eq(extension, "atr")) {offset = 7*16;}

      draw_text(x+40, y+6, favorite, false, row == BROWSER_SEL, false);
      draw_media(x, y, row == BROWSER_SEL, offset);
      if(row == BROWSER_SEL) {
        sprintf(favorite, "%s.%s", favorite, extension);
        strcpy(ROM.name, favorite);
        strcpy(ROM.art, remove_ext(ROM.name, '.', '/'));
        strcpy(ROM.path, path);
        strcpy(ROM.ext, extension);
        ROM.ready = true;
      }
    }

    /* Draw scrollbar */
    if (ROMS.total > BROWSER_LIMIT) {
      int bar_x = 788;
      int bar_h = BROWSER_LIMIT * row_h;
      int thumb_h = (BROWSER_LIMIT * bar_h) / ROMS.total;
      if (thumb_h < 8) thumb_h = 8;
      int thumb_y = y_start + (ROMS.offset * (bar_h - thumb_h)) / (ROMS.total - 1);
      draw_mask(bar_x, y_start, 8, bar_h);
      for (int i = 0; i < thumb_h * 8; i++) buffer[i] = GUI.fg;
      ili9341_write_frame_rectangleLE(bar_x, thumb_y, 8, thumb_h, buffer);
    }

    /*
    printf("\n\n***********"
      "\nROM details"
      "\n- ROM.name ->\t%s"
      "\n- ROM.art ->\t%s"
      "\n- ROM.path ->\t%s"
      "\n- ROM.ext ->\t%s"
      "\n- ROM.ready ->\t%d"
      "\n***********\n\n",
      ROM.name, ROM.art, ROM.path, ROM.ext, ROM.ready);
    */
    // printf("\n----- %s END -----", __func__);
  }

  void delete_recents() {
    //  printf("\n----- %s START -----", __func__);
    char file[256] = "/sd/odroid/data";
    sprintf(file, "%s/%s", file, RETROESP_FOLDER);
    sprintf(file, "%s/%s", file, RECENT_FILE);

    FILE *f;
    f = fopen(file, "w+");
    //  printf("\nCLOSING: %s", file);
    fclose(f);
    //  printf("\n----- %s END -----\n", __func__);

    ROM_COUNTS[2] = 0;

    draw_background();
    char message[100] = "deleting...";
    int w = strlen(message) * 20;
    int x_del = (WIDTH - w) / 2;
    int y = (480 - 32) / 2;
    draw_text(x_del, y, message, false, false, false);

    int bar_y = y + 40;
    int bar_h = 4;
    for(int n = 0; n < w; n+=4) {
      int bw = ((n+4) <= w) ? 4 : w - n;
      for(int i = 0; i < bw * bar_h; i++) buffer[i] = GUI.hl;
      ili9341_write_frame_rectangleLE(x_del+n, bar_y, bw, bar_h, buffer);
      usleep(5000);
    }

    draw_background();
    draw_systems();
    draw_text(16,4,EMULATORS[STEP],false,true, false);
    draw_settings();

  }  
//}#pragma endregion Recents

//{#pragma region Cover
  void get_cover() {
    preview_cover(false);
  }

  void preview_cover(bool error) {
    ROM.crc = 0;

    int bw = 224;
    int bh = 300;
    int i = 0;

    char file[256] = "/sd/romart";
    char ext[10];
    STEP != 1 && STEP != 2? sprintf(ext, "%s", DIRECTORIES[STEP]) : sprintf(ext, "%s", ROM.ext);
    sprintf(file, "%s/%s/%s.art", file, ext, ROM.art);

    if(!error) {
      FILE *f = fopen(file, "rb");
      if(f) {
        uint16_t width, height;
        fread(&width, 2, 1, f);
        fread(&height, 2, 1, f);
        bw = width * 2;
        bh = height * 2;
        if (bw > 400) bw = 400;
        if (bh > 400) bh = 400;
        ROM.crc = 1;
        fclose(f);
      } else {
        error = true;
      }
    }

    //  printf("\n----- %s -----\n%s\n", __func__, file);
    for(int h = 0; h < bh; h++) {
      for(int w = 0; w < bw; w++) {
        buffer[i] = (h == 0) || (h == bh -1) ? GUI.hl : (w == 0) ||  (w == bw -1) ? GUI.hl : GUI.bg;
        i++;
      }
    }
    int x = SCREEN.w-48-bw;
    int y = POS.y+16;
    ili9341_write_frame_rectangleLE(x, y, bw, bh, buffer);

    int center = x + bw/2;
    center -= error ? 100 : 70;

    draw_text(center, y + (bh/2) - 16, error ? (char *)"NO PREVIEW" : (char *)"PREVIEW", false, false, false);

    if(ROM.crc == 1) {
      usleep(20000);
      draw_cover();
    }
  }

  void draw_cover() {
    //  printf("\n----- %s -----\n%s\n", __func__, "OPENNING");
    char file[256] = "/sd/romart";
    char ext[10];
    STEP != 1 && STEP != 2? sprintf(ext, "%s", DIRECTORIES[STEP]) : sprintf(ext, "%s", ROM.ext);
    sprintf(file, "%s/%s/%s.art", file, ext, ROM.art);

    FILE *f = fopen(file, "rb");
    if(f) {
    //  printf("\n----- %s -----\n%s\n", __func__, "OPEN");
      uint16_t width, height;
      fread(&width, 2, 1, f);
      fread(&height, 2, 1, f);

      int dst_w = width * 2;
      int dst_h = height * 2;
      int x = SCREEN.w-48-dst_w;
      int y = POS.y+16;

      if (width<=WIDTH && height<=480) {
        /* Read source row-by-row and 2x upscale into buffer, render in strips */
        uint16_t *row_buf = (uint16_t*)heap_caps_malloc(width*2, MALLOC_CAP_SPIRAM);
        if (row_buf) {
          int strip_rows = 64000 / dst_w; /* max dst rows per strip */
          if (strip_rows < 2) strip_rows = 2;
          strip_rows &= ~1; /* make even for 2x */
          int src_strip = strip_rows / 2;
          for (int sr = 0; sr < (int)height; sr += src_strip) {
            int rows = (sr + src_strip <= (int)height) ? src_strip : ((int)height - sr);
            int bi = 0;
            for (int r = 0; r < rows; r++) {
              fread(row_buf, 2, width, f);
              for (int pass = 0; pass < 2; pass++) {
                for (int c = 0; c < (int)width; c++) {
                  buffer[bi++] = row_buf[c];
                  buffer[bi++] = row_buf[c];
                }
              }
            }
            ili9341_write_frame_rectangleLE(x, y + sr*2, dst_w, rows*2, buffer);
          }
          heap_caps_free(row_buf);
        }
      } else {
      //  printf("\n----- %s -----\n%s\nwidth:%d height:%d\n", __func__, "ERROR", width, height);
        preview_cover(true);
      }

      fclose(f);
    }
  }
//}#pragma endregion Cover

//{#pragma region Animations
  /*
   * clean_up - Wrap off-screen system icon positions around the carousel
   */
  void clean_up() {
    int MAX = 400 + (COUNT * GAP);
    for (int n = 0; n < COUNT; n++) {
      if (SYSTEMS[n].x > COUNT * GAP - 128) {
        SYSTEMS[n].x -= MAX;
      }
      if (SYSTEMS[n].x <= (-64 - (COUNT / 2) * GAP)) {
        SYSTEMS[n].x += MAX;
      }
    }
  }

  void animate(int dir) {

    /* --- Try to load new system's artwork for a smooth transition --- */
    uint16_t *new_art = (STEP != 0) ? load_artwork_to_buf(STEP) : NULL;

    if (new_art) {
      /*
       * ARTWORK WIPE TRANSITION
       * Draw the final black header + icons first, then wipe artwork
       * strips over the old content.  Nothing is drawn underneath the
       * artwork â€” every pixel is written exactly once.
       */
      int art_w = WIDTH, art_h = HEIGHT - 30, art_y = 30;
      int steps = 10;
      int strip_w = art_w / steps;

      /* Draw the 30px black header with file count and status icons
         BEFORE the wipe so it's visible immediately. */
      for (int i = 0; i < WIDTH * 30; i++) buffer[i] = 0x0000;
      ili9341_write_frame_rectangleLE(0, 0, WIDTH, 30, buffer);

      {
        uint16_t saved_bg = GUI.bg, saved_fg = GUI.fg;
        GUI.bg = 0x0000;
        GUI.fg = 0xFFE0;
        char cnt[32];
        if (SHORT_NAMES[STEP][0])
          sprintf(cnt, "%s (%d files)", SHORT_NAMES[STEP], ROM_COUNTS[STEP]);
        else
          sprintf(cnt, "%d files", ROM_COUNTS[STEP]);
        draw_text(8, 0, cnt, false, false, false);
        GUI.bg = saved_bg;
        GUI.fg = saved_fg;
      }

      /* Status icons on black header */
      {
        uint16_t saved_bg = GUI.bg;
        GUI.bg = 0x0000;

        /* Contrast */
        {
          int32_t dy = 0;
          switch (BRIGHTNESS) {
            case 10: case 9: case 8: dy = 0; break;
            case 7: case 6: case 5: dy = 16; break;
            case 4: case 3: case 2: dy = 32; break;
            default: dy = 48; break;
          }
          int i = 0, x = SCREEN.w - 144;
          for (int h = 0; h < 16; h++) {
            for (int sh = 0; sh < 2; sh++) {
              for (int w = 0; w < 16; w++) {
                buffer[i++] = brightness[dy + h][w] == WHITE ? GUI.hl : 0x0000;
                buffer[i++] = brightness[dy + h][w] == WHITE ? GUI.hl : 0x0000;
              }
            }
          }
          ili9341_write_frame_rectangleLE(x, 0, 32, 32, buffer);
        }

        /* Speaker */
        {
          int32_t vol = get_volume();
          int dh = 0;
          switch (vol) {
            case 0: dh = 64; break;
            case 1: case 2: case 3: dh = 48; break;
            case 4: case 5: dh = 32; break;
            case 6: case 7: dh = 16; break;
            default: dh = 0; break;
          }
          int i = 0, x = SCREEN.w - 104;
          for (int h = 0; h < 16; h++) {
            for (int sh = 0; sh < 2; sh++) {
              for (int w = 0; w < 16; w++) {
                buffer[i++] = speaker[dh + h][w] == WHITE ? GUI.hl : 0x0000;
                buffer[i++] = speaker[dh + h][w] == WHITE ? GUI.hl : 0x0000;
              }
            }
          }
          ili9341_write_frame_rectangleLE(x, 0, 32, 32, buffer);
        }

        /* Battery */
        #ifdef BATTERY
        {
          odroid_input_battery_level_read(&battery_state);
          int i = 0, x = SCREEN.w - 64;
          for (int h = 0; h < 16; h++) {
            for (int sh = 0; sh < 2; sh++) {
              for (int w = 0; w < 16; w++) {
                buffer[i++] = battery[h][w] == WHITE ? GUI.hl : 0x0000;
                buffer[i++] = battery[h][w] == WHITE ? GUI.hl : 0x0000;
              }
            }
          }
          ili9341_write_frame_rectangleLE(x, 0, 32, 32, buffer);

          int pct = battery_state.percentage / 10;
          int bw = pct > 0 ? (pct > 10 ? 10 : pct) : 10;
          int color[11] = {24576,24576,64288,64288,65504,65504,65504,26592,26592,26592,26592};
          i = 0;
          for (int c = 0; c < 8; c++)
            for (int n = 0; n <= bw * 2; n++)
              buffer[i++] = color[bw];
          ili9341_write_frame_rectangleLE(x + 4, 12, bw * 2, 8, buffer);
        }
        #endif

        /* USB gamepad icon */
        {
          bool usb = odroid_input_usb_gamepad_connected();
          int i = 0, x = SCREEN.w - 184;
          for (int h = 0; h < 16; h++) {
            for (int sh = 0; sh < 2; sh++) {
              for (int w = 0; w < 16; w++) {
                buffer[i++] = usb_gamepad_icon[h][w] == WHITE
                              ? (usb ? GUI.hl : 0x4208) : 0x0000;
                buffer[i++] = usb_gamepad_icon[h][w] == WHITE
                              ? (usb ? GUI.hl : 0x4208) : 0x0000;
              }
            }
          }
          ili9341_write_frame_rectangleLE(x, 0, 32, 32, buffer);
        }

        /* GPIO gamepad icon */
        {
          bool gpio = odroid_input_gpio_pad_detected();
          int i = 0, x = SCREEN.w - 224;
          for (int h = 0; h < 16; h++) {
            for (int sh = 0; sh < 2; sh++) {
              for (int w = 0; w < 16; w++) {
                buffer[i++] = gpio_gamepad_icon[h][w] == WHITE
                              ? (gpio ? GUI.hl : 0x4208) : 0x0000;
                buffer[i++] = gpio_gamepad_icon[h][w] == WHITE
                              ? (gpio ? GUI.hl : 0x4208) : 0x0000;
              }
            }
          }
          ili9341_write_frame_rectangleLE(x, 0, 32, 32, buffer);
        }

        GUI.bg = saved_bg;
      }

      display_flush();

      /* Wipe artwork strips over the old content */
      for (int s = 0; s < steps; s++) {
        int x0, w;
        if (dir == 1) {
          x0 = s * strip_w;
          w = strip_w;
          if (s == steps - 1) w = art_w - x0;
        } else {
          x0 = art_w - (s + 1) * strip_w;
          if (x0 < 0) x0 = 0;
          w = strip_w;
          if (s == steps - 1) w = art_w - x0;
        }

        int max_rows = 64000 / w;
        if (max_rows < 1) max_rows = 1;
        if (max_rows > art_h) max_rows = art_h;

        for (int row = 0; row < art_h; row += max_rows) {
          int rows = (row + max_rows <= art_h) ? max_rows : (art_h - row);
          int bi = 0;
          for (int r = 0; r < rows; r++) {
            uint16_t *src_row = &new_art[(row + r) * art_w + x0];
            for (int c = 0; c < w; c++) {
              buffer[bi++] = src_row[c];
            }
          }
          ili9341_write_frame_rectangleLE(x0, art_y + row, w, rows, buffer);
        }

        display_flush();
        usleep(15000);
      }

      heap_caps_free(new_art);

      /* Snap icon positions to final state for this STEP */
      for (int n = 0; n < COUNT; n++) {
        int delta = (n - STEP);
        if (delta < 0)        SYSTEMS[n].x = (GAP/3) + (GAP * delta);
        else if (delta == 0)  SYSTEMS[n].x = (GAP/3);
        else if (delta == 1)  SYSTEMS[n].x = GAP/3 + NEXT;
        else if (delta == 2)  SYSTEMS[n].x = GAP/3 + NEXT + GAP;
        else                  SYSTEMS[n].x = GAP/3 + NEXT + (GAP * (delta - 1));
      }

    } else {
      /*
       * STANDARD TRANSITION (no artwork available)
       * Original sliding icon animation with fallback logo/hint
       */
      delete_numbers();
      draw_mask(0, 0, SCREEN.w - 112, 40);
      draw_text(16, 4, EMULATORS[STEP], false, true, false);
      /* Show file count with short name after system name */
      if (STEP >= 1 && ROM_COUNTS[STEP] >= 0) {
        uint16_t saved_fg = GUI.fg;
        GUI.fg = 0xFFE0;
        char cnt[32];
        if (SHORT_NAMES[STEP][0])
          sprintf(cnt, "%s (%d files)", SHORT_NAMES[STEP], ROM_COUNTS[STEP]);
        else
          sprintf(cnt, "%d files", ROM_COUNTS[STEP]);
        int name_len = strlen(EMULATORS[STEP]);
        int cx = 16 + (name_len + 1) * 20;
        draw_text(cx, 4, cnt, false, false, false);
        GUI.fg = saved_fg;
      }
      draw_contrast();

      int y = POS.y + 92;
      for (int i = 0; i < 4; i++) draw_mask(0, y+(i*80)-12, WIDTH, 80);
      int sx[4][13] = {
        {20,20,10,10,10,8,8,7,7,5,5,5,5},
        {75,65,50,50,45,45,40,40,30,30,20,20,10}
      };
      for (int i = 0; i < 13; i++) {
        if (dir == -1) {
          for (int e = 0; e < COUNT; e++) {
            SYSTEMS[e].x += STEP != COUNT - 1 ?
              STEP == (e-1) ? sx[1][i] : sx[0][i] :
              e == 0 ? sx[1][i] : sx[0][i] ;
          }
        } else {
          for (int e = 0; e < COUNT; e++) {
            SYSTEMS[e].x -= STEP == e ? sx[1][i] : sx[0][i];
          }
        }
        draw_mask(0, 64, WIDTH, 80);
        draw_mask(0, 144, WIDTH, 24);
        draw_systems();
        display_flush();
        usleep(20000);
      }

      if (STEP == 0) {
        for (int s = 0; s < 6; s++) draw_mask(0, s * 80, WIDTH, 80);
        draw_battery();
        draw_speaker();
        draw_contrast();
        draw_systems();
        draw_text(16,4,EMULATORS[STEP], false, true, false);
        draw_settings();
      } else {
        draw_system_logo();
        char hint[] = "press a to browse";
        int cx = (WIDTH - strlen(hint) * 28) / 2;
        draw_text_scaled(cx, 440, hint, GUI.fg);
      }
    }
    clean_up();
    display_flush();
  }

  void restore_layout() {

    SYSTEMS[0].x = GAP/3;
    for(int n = 1; n < COUNT; n++) {
      if(n == 1) {
        SYSTEMS[n].x = GAP/3+NEXT;
      } else if(n == 2) {
        SYSTEMS[n].x = GAP/3+NEXT+(GAP);
      } else {
        SYSTEMS[n].x = GAP/3+NEXT+(GAP*(n-1));
      }
    };

    draw_background();

    for(int n = 0; n < COUNT; n++) {
      int delta = (n-STEP);
      if(delta < 0) {
        SYSTEMS[n].x = (GAP/3) + (GAP * delta);
      } else if(delta == 0) {
        SYSTEMS[n].x = (GAP/3);
      } else if(delta == 1) {
        SYSTEMS[n].x = GAP/3+NEXT;
      } else if(delta == 2) {
        SYSTEMS[n].x = GAP/3+NEXT+(GAP);
      } else {
        SYSTEMS[n].x = GAP/3+NEXT+(GAP*(delta-1));
      }
    }

    clean_up();
    draw_systems();
    draw_text(16,4,EMULATORS[STEP],false,true, false);

    /* Show file count with short name in header for emulator systems */
    if (STEP >= 1 && ROM_COUNTS[STEP] >= 0) {
      uint16_t saved_bg = GUI.bg, saved_fg = GUI.fg;
      GUI.bg = GUI.bg;
      GUI.fg = 0xFFE0; /* yellow */
      char cnt[32];
      if (SHORT_NAMES[STEP][0])
        sprintf(cnt, "%s (%d files)", SHORT_NAMES[STEP], ROM_COUNTS[STEP]);
      else
        sprintf(cnt, "%d files", ROM_COUNTS[STEP]);
      /* Right-align or place after system name */
      int name_len = strlen(EMULATORS[STEP]);
      int cx = 16 + (name_len + 1) * 20;
      draw_text(cx, 4, cnt, false, false, false);
      GUI.bg = saved_bg;
      GUI.fg = saved_fg;
    }

    /* In carousel mode, show a hint instead of auto-loading ROM list */
    if (!BROWSER) {
      if (STEP == 0) {
        draw_settings();
      } else {
        /* Show system artwork (PNG) or fallback to logo + hint */
        if (!show_system_artwork()) {
          draw_system_logo();
          const char *hint = "press a to browse";
          int cx = (WIDTH - strlen(hint) * 28) / 2;
          draw_text_scaled(cx, 440, hint, GUI.fg);
        }
      }
    } else {
      STEP == 0 ? draw_settings() :
        STEP == 1 ? get_favorites() :
        STEP == 2 ? get_recents() :
        get_files();
    }
    int MAX = 400+(COUNT*GAP);
    for(int n = 0; n < COUNT; n++) {
      if(SYSTEMS[n].x > COUNT*GAP-128) {
        SYSTEMS[n].x -= MAX;
      }
      if(SYSTEMS[n].x <= (-64 - (COUNT/2) * GAP)) {
        SYSTEMS[n].x += MAX;
      }
    }
    display_flush();
  }
//}#pragma endregion Animations

//{#pragma region Boot Screens
  /*â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    show_png_logo_native() â€” Load /sd/boot_logo.png and display it
    scaled 2Ã— to fill the full 480Ã—800 LCD panel.  Uses PPA to
    rotate 270Â° + scale (matching panel orientation).
    Returns 1 on success (image is already on LCD), 0 on failure.
  â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€*/
  static int show_png_logo_native(void)
  {
#ifdef CONFIG_HDMI_OUTPUT
    /* HDMI: load PNG and NN-scale to fill WIDTH×HEIGHT into framebuffer */
    pngObject png = {0};
    if (!loadPngFromFileRaw("/sd/boot_logo.png", &png, true, true)) return 0;
    if (png.w == 0 || png.h == 0 || !png.data) return 0;

    uint16_t *src = (uint16_t *)png.data;
    int pw = png.w, ph = png.h;
    int dst_w = WIDTH, dst_h = HEIGHT;

    /* NN scale in strips that fit buffer[64000], byte-swap + R<->B swap */
    int max_rows = 64000 / dst_w;
    if (max_rows < 1) max_rows = 1;
    for (int dr = 0; dr < dst_h; dr += max_rows) {
      int rows = (dr + max_rows <= dst_h) ? max_rows : (dst_h - dr);
      int bi = 0;
      for (int r = 0; r < rows; r++) {
        int sr = (dr + r) * ph / dst_h;
        uint16_t *srow = &src[sr * pw];
        for (int c = 0; c < dst_w; c++) {
          int sc = c * pw / dst_w;
          uint16_t p = srow[sc];
          uint16_t bs = (p >> 8) | (p << 8);
          buffer[bi++] = ((bs & 0x1F) << 11) | (bs & 0x07E0) | ((bs >> 11) & 0x1F);
        }
      }
      ili9341_write_frame_rectangleLE(0, dr, dst_w, rows, buffer);
    }
    free(src);
    display_flush();
    return 1;
#else
    pngObject png = {0};
    if (!loadPngFromFile("/sd/boot_logo.png", &png, true, false)) return 0;
    if (png.w == 0 || png.h == 0 || !png.data) return 0;

    int pw = png.w, ph = png.h;
    uint16_t *src = (uint16_t *)png.data;

    /* Allocate DMA-aligned input for PPA (pngAux buffer may not be aligned) */
    size_t in_size = pw * ph * 2;
    size_t aligned_in = (in_size + 63) & ~63;
    void *ppa_in = heap_caps_aligned_calloc(64, 1, aligned_in,
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!ppa_in) { free(src); return 0; }
    memcpy(ppa_in, src, in_size);
    free(src);

    /* After 270Â° rotation, dimensions swap: rotated_w = ph, rotated_h = pw.
       Force 2Ã— scale to fill 480Ã—800 LCD from a 240Ã—400 (or similar) logo. */
    uint16_t lcd_w = st7701_lcd_width();   /* 480 */
    uint16_t lcd_h = st7701_lcd_height();  /* 800 */
    float scale_x = (float)lcd_w / (float)ph;   /* fit rotated width  */
    float scale_y = (float)lcd_h / (float)pw;   /* fit rotated height */

    /* PPA rotate 270Â° + scale â†’ output fills LCD */
    uint32_t out_w = lcd_w;
    uint32_t out_h = lcd_h;
    size_t out_size = out_w * out_h * 2;
    size_t aligned_out = (out_size + 63) & ~63;
    void *ppa_out = heap_caps_aligned_calloc(64, 1, aligned_out,
                        MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!ppa_out) { heap_caps_free(ppa_in); return 0; }

    esp_err_t ret = ppa_rotate_scale_rgb565_to(
        ppa_in, pw, ph, 270, scale_x, scale_y,
        ppa_out, aligned_out, &out_w, &out_h, false);
    heap_caps_free(ppa_in);
    if (ret != ESP_OK) { heap_caps_free(ppa_out); return 0; }

    /* Composite into a full-frame buffer */
    size_t frame_size = lcd_w * lcd_h * sizeof(uint16_t);
    size_t frame_aligned = (frame_size + 63) & ~63;
    uint16_t *frame = (uint16_t *)heap_caps_aligned_calloc(64, 1, frame_aligned,
                          MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
    if (!frame) { heap_caps_free(ppa_out); return 0; }

    /* Blit rotated PNG into center of full frame */
    uint16_t x = (lcd_w > out_w) ? (lcd_w - out_w) / 2 : 0;
    uint16_t y = (lcd_h > out_h) ? (lcd_h - out_h) / 2 : 0;
    uint16_t *rotated = (uint16_t *)ppa_out;
    uint32_t blit_w = (out_w <= lcd_w) ? out_w : lcd_w;
    uint32_t blit_h = (out_h <= lcd_h) ? out_h : lcd_h;
    for (uint32_t row = 0; row < blit_h; row++) {
      memcpy(&frame[(y + row) * lcd_w + x],
             &rotated[row * out_w],
             blit_w * sizeof(uint16_t));
    }
    heap_caps_free(ppa_out);

    st7701_lcd_draw_bitmap(0, 0, lcd_w, lcd_h, frame);
    vTaskDelay(pdMS_TO_TICKS(50));
    heap_caps_free(frame);
    return 1;
#endif /* !CONFIG_HDMI_OUTPUT */
  }

  /*â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
    load_bmp_logo() â€” Try to load /sd/boot_logo.bmp into buffer[].
    Supports 24-bit uncompressed BMP, max 800Ã—480, w*h â‰¤ 64000.
    Returns 1 on success (buffer filled with RGB565, w/h/x/y set).
    Returns 0 on failure (file missing, bad format, too large).
  â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€*/
  static int load_bmp_logo(int *out_w, int *out_h, int *out_x, int *out_y)
  {
    FILE *f = fopen("/sd/boot_logo.bmp", "rb");
    if (!f) return 0;

    /* Read BMP file header (14 bytes) + DIB header start (40 bytes) */
    uint8_t hdr[54];
    if (fread(hdr, 1, 54, f) != 54)             { fclose(f); return 0; }
    if (hdr[0] != 'B' || hdr[1] != 'M')         { fclose(f); return 0; }

    /* Parse DIB header (BITMAPINFOHEADER) */
    uint32_t data_offset = hdr[10] | (hdr[11]<<8) | (hdr[12]<<16) | (hdr[13]<<24);
    int32_t  bmp_w = (int32_t)(hdr[18] | (hdr[19]<<8) | (hdr[20]<<16) | (hdr[21]<<24));
    int32_t  bmp_h = (int32_t)(hdr[22] | (hdr[23]<<8) | (hdr[24]<<16) | (hdr[25]<<24));
    uint16_t bpp   = hdr[28] | (hdr[29]<<8);
    uint32_t compr = hdr[30] | (hdr[31]<<8) | (hdr[32]<<16) | (hdr[33]<<24);

    /* Validate: 24-bit uncompressed, reasonable size */
    int flip = 1;  /* bottom-up by default */
    int abs_h = bmp_h;
    if (bmp_h < 0) { abs_h = -bmp_h; flip = 0; }  /* top-down BMP */

    if (bpp != 24 || compr != 0)                 { fclose(f); return 0; }
    if (bmp_w <= 0 || bmp_w > WIDTH)               { fclose(f); return 0; }
    if (abs_h <= 0 || abs_h > 480)               { fclose(f); return 0; }
    if ((int)bmp_w * abs_h > 64000)              { fclose(f); return 0; }

    /* BMP rows are padded to 4-byte boundaries */
    int row_bytes = bmp_w * 3;
    int row_pad   = (4 - (row_bytes % 4)) % 4;
    int row_total = row_bytes + row_pad;

    /* Seek to pixel data */
    fseek(f, data_offset, SEEK_SET);

    /* Read row-by-row, converting BGR888 â†’ RGB565 into buffer[] */
    uint8_t row_buf[WIDTH*3 + 4];  /* max WIDTH*3 = 2400 bytes per row */
    for (int r = 0; r < abs_h; r++) {
      int dest_row = flip ? (abs_h - 1 - r) : r;
      if (fread(row_buf, 1, row_total, f) != (size_t)row_total) {
        fclose(f); return 0;
      }
      for (int c = 0; c < bmp_w; c++) {
        uint8_t b = row_buf[c * 3 + 0];
        uint8_t g = row_buf[c * 3 + 1];
        uint8_t rd = row_buf[c * 3 + 2];
        /* RGB888 â†’ RGB565 */
        buffer[dest_row * bmp_w + c] =
            ((rd >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
      }
    }
    fclose(f);

    *out_w = bmp_w;
    *out_h = abs_h;
    *out_x = (WIDTH - bmp_w) / 2;
    *out_y = (480 - abs_h) / 2;
    return 1;
  }

  /*â”€â”€ Helper: render the built-in logo3d into buffer[] and blit â”€â”€*/
  static void draw_builtin_logo(void)
  {
    int src_w = 280, src_h = 100;
    int dst_w = src_w * 2, dst_h = src_h * 2;
    int x = (SCREEN.w - dst_w) / 2;
    int y = 56;

    /* Compute mid-tone color: blend GUI.hl and GUI.fg */
    uint16_t mid_color;
    {
      uint16_t r1 = (GUI.hl >> 11) & 0x1F;
      uint16_t g1 = (GUI.hl >> 5)  & 0x3F;
      uint16_t b1 =  GUI.hl        & 0x1F;
      uint16_t r2 = (GUI.fg >> 11) & 0x1F;
      uint16_t g2 = (GUI.fg >> 5)  & 0x3F;
      uint16_t b2 =  GUI.fg        & 0x1F;
      mid_color = (((r1 + r2) / 2) << 11)
                | (((g1 + g2) / 2) << 5)
                |  ((b1 + b2) / 2);
    }

    /* Render in strips of 10 source rows (20 dst rows) = 560*20 = 11200 pixels per strip */
    int strip_rows = 10;
    for (int sr = 0; sr < src_h; sr += strip_rows) {
      int rows = (sr + strip_rows <= src_h) ? strip_rows : (src_h - sr);
      int i = 0;
      for (int r = sr; r < sr + rows; r++) {
        for (int pass = 0; pass < 2; pass++) {
          for (int c = 0; c < src_w; c++) {
            uint16_t px = logo3d[r][c];
            uint16_t col;
            if (px == 0) {
              col = GUI.bg;
            } else if (px == 65535) {
              col = GUI.hl;
            } else if (px == 33808) {
              col = mid_color;
            } else {
              col = GUI.fg;
            }
            buffer[i++] = col;
            buffer[i++] = col;
          }
        }
      }
      ili9341_write_frame_rectangleLE(x, y + sr * 2, dst_w, rows * 2, buffer);
    }
  }

  /*â”€â”€ Helper: wait up to ms milliseconds, return early if A pressed â”€â”€*/
  static void wait_or_button(int ms)
  {
    odroid_gamepad_state st;
    int elapsed = 0;
    while (elapsed < ms) {
      odroid_input_gamepad_read(&st);
      if (st.values[ODROID_INPUT_A]) return;
      usleep(50000);   /* poll every 50ms */
      elapsed += 50;
    }
  }

  void splash() {
    draw_background();

    /* â”€â”€ Phase 1: main logo (SD PNG native â†’ SD BMP â†’ built-in), 5 sec, A to skip â”€â”€ */
    int png_native = show_png_logo_native();

    if (!png_native) {
      int w, h, x, y;
      int from_sd = load_bmp_logo(&w, &h, &x, &y);
      if (from_sd) {
        /* BMP loaded from SD card â€” buffer[] already has RGB565 pixels */
        ili9341_write_frame_rectangleLE(x, y, w, h, buffer);
      } else {
        draw_builtin_logo();
      }
    }

    if (!png_native) {
      /* BUILD string at bottom */
      char message[100] = BUILD;
      int width = strlen(message)*20;
      int center = ceil((WIDTH)-(width))-96;
      int y = 440;
      draw_text(center,y,message,false,false, false);
      display_flush();
    }

    wait_or_button(5000);

    /* Clear full LCD so no leftovers from native PNG remain in the
       80-pixel bands that display_flush() does not cover. */
#ifndef CONFIG_HDMI_OUTPUT
    if (png_native) {
      uint16_t lw = st7701_lcd_width();
      uint16_t lh = st7701_lcd_height();
      size_t fsz = (lw * lh * sizeof(uint16_t) + 63) & ~63;
      uint16_t *blk = (uint16_t *)heap_caps_aligned_calloc(64, 1, fsz,
                           MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA);
      if (blk) {
        st7701_lcd_draw_bitmap(0, 0, lw, lh, blk);
        vTaskDelay(pdMS_TO_TICKS(50));
        heap_caps_free(blk);
      }
      /* Skip built-in logo + credits when custom cover was shown */
      draw_background();
      display_flush();
      return;
    }
#endif /* !CONFIG_HDMI_OUTPUT */

    /* â”€â”€ Phase 2: built-in logo + credit, 2 seconds â”€â”€ */
    draw_background();
    draw_builtin_logo();

    /* Credit text below the logo */
    {
      char credit[] = "Done by Claude Opus 4.6";
      int cw = strlen(credit) * 20;
      int cx = (WIDTH - cw) / 2;
      draw_text(cx, 280, credit, false, false, false);
    }
    display_flush();

    sleep(2);
    draw_background();
    display_flush();
  }

  void boot() {
    draw_background();
    char message[100] = "retro esp32";
    int w = strlen(message)*20;
    int x = (WIDTH - w) / 2;
    int y = (480 - 32) / 2;
    draw_text(x, y, message, false, false, false);

    int bar_y = y + 40;
    int bar_h = 4;
    for(int n = 0; n < w; n+=4) {
      int bw = ((n+4) <= w) ? 4 : w - n;
      for(int i = 0; i < bw * bar_h; i++) buffer[i] = GUI.hl;
      ili9341_write_frame_rectangleLE(x+n, bar_y, bw, bar_h, buffer);
      display_flush();
      usleep(5000);
    }
  }


  void restart() {
    draw_background();

    char message[100] = "restarting";
    int w = strlen(message) * 20;
    int x = (WIDTH - w) / 2;
    int y = (480 - 32) / 2;
    draw_text(x, y, message, false, false, false);

    int bar_y = y + 40;
    int bar_h = 4;
    for(int n = 0; n < w; n+=4) {
      int bw = ((n+4) <= w) ? 4 : w - n;
      for(int i = 0; i < bw * bar_h; i++) buffer[i] = GUI.hl;
      ili9341_write_frame_rectangleLE(x+n, bar_y, bw, bar_h, buffer);
      display_flush();
      usleep(5000);
    }
  }
//}#pragma endregion Boot Screens

//{#pragma region ROM Options
  /* Forward declarations for post-emulator UI restore */
  void draw_browser_screen(void);
  void draw_carousel_screen(void);

  void rom_run(bool resume) {

    set_restore_states();

    draw_background();
    char *message = !resume ? (char *)"loading..." : (char *)"hold start";

    int w = strlen(message) * 20;
    int x = (WIDTH - w) / 2;
    int y = (480 - 32) / 2;
    draw_text(x, y, message, false, false, false);

    int bar_y = y + 40;
    int bar_h = 4;
    for(int n = 0; n < w; n+=4) {
      int bw = ((n+4) <= w) ? 4 : w - n;
      for(int i = 0; i < bw * bar_h; i++) buffer[i] = GUI.hl;
      ili9341_write_frame_rectangleLE(x+n, bar_y, bw, bar_h, buffer);
      display_flush();
      usleep(5000);
    }

    /* Build the full ROM path */
    char rom_path[256] = "";
    sprintf(rom_path, "%s/%s", ROM.path, ROM.name);

    /* â”€â”€ PSRAM App: handle .papp files via PSRAM XIP loader â”€â”€â”€â”€â”€â”€â”€â”€â”€ */
    if (ext_eq(ROM.ext, "papp")) {

      /* Save ROM path to NVS so the PSRAM app can retrieve it */
      odroid_settings_RomFilePath_set(rom_path);

      /* serial_upload_deinit();  // disabled for debug */

      psram_app_handle_t papp = NULL;
      esp_err_t err = psram_app_load(rom_path, &papp);

      if (err != ESP_OK) {
        serial_upload_init();
        get_restore_states();
        LAUNCHER = false;
        draw_carousel_screen();
        return;
      }

      /* Run the PSRAM app â€” blocks until it returns */
      int result = psram_app_run(papp);
      (void)result;

      /* Reset the cached I2S sample rate so the next app can safely
       * reconfigure without calling i2s_channel_disable() (which can
       * hang if DMA never fully drained). */
      audio_reset_sample_rate();

      /* Cleanup */
      psram_app_unload(papp);

      /* serial_upload_init();  // disabled for debug */

      /* Restore launcher UI */
      get_restore_states();
      LAUNCHER = false;
      BROWSER = false;
      draw_carousel_screen();
      return;
    }

    /* Save ROM path and resume state to NVS for the emulator app */
    odroid_settings_RomFilePath_set(rom_path);
    odroid_settings_DataSlot_set(resume ? 1 : 0);
    if (resume) {
      odroid_settings_StartAction_set(ODROID_START_ACTION_RESTART);
    }

    /* Find the OTA slot for this ROM type */
    int ota_slot = get_ota_slot(ROM.ext);
    printf("rom_run: ROM='%s' ext='%s' ota_slot=%d\n",
           ROM.name, ROM.ext, ota_slot);

    if (ota_slot < 0) {
      printf("rom_run: unsupported ROM type: %s\n", ROM.ext);
      /* Restore UI and return */
      get_restore_states();
      LAUNCHER = false;
      if (BROWSER && ROMS.total > 0) {
          seek_files();
          draw_browser_screen();
      } else {
          draw_carousel_screen();
      }
      return;
    }

    /* Switch to the emulator's OTA partition and reboot */
    const esp_partition_t *emu_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_OTA_MIN + ota_slot,
        NULL);
    if (emu_part == NULL) {
      printf("rom_run: OTA partition ota_%d not found!\n", ota_slot);
      get_restore_states();
      LAUNCHER = false;
      draw_carousel_screen();
      return;
    }

    printf("rom_run: Switching to partition '%s' at 0x%lx, rebooting...\n",
           emu_part->label, (unsigned long)emu_part->address);

    /* Black screen + backlight off to avoid white flash during reboot */
    ili9341_clear(0x0000);
    display_flush();
    ili9341_poweroff();
    vTaskDelay(pdMS_TO_TICKS(50));

    esp_ota_set_boot_partition(emu_part);
    esp_restart();
    /* Does not return */
  }

  void rom_resume() {
    rom_run(true);
  }

  void launch_standalone_app(int ota_slot) {
    draw_background();
    char *message = (char *)"loading...";
    int w = strlen(message) * 20;
    int x = (WIDTH - w) / 2;
    int y = (480 - 32) / 2;
    draw_text(x, y, message, false, false, false);

    int bar_y = y + 40;
    int bar_h = 4;
    for(int n = 0; n < w; n+=4) {
      int bw = ((n+4) <= w) ? 4 : w - n;
      for(int i = 0; i < bw * bar_h; i++) buffer[i] = GUI.hl;
      ili9341_write_frame_rectangleLE(x+n, bar_y, bw, bar_h, buffer);
      display_flush();
      usleep(5000);
    }

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        ESP_PARTITION_SUBTYPE_APP_OTA_MIN + ota_slot,
        NULL);
    if (part == NULL) {
      printf("launch_standalone_app: OTA partition ota_%d not found!\n", ota_slot);
      draw_carousel_screen();
      return;
    }

    printf("launch_standalone_app: Switching to '%s' at 0x%lx\n",
           part->label, (unsigned long)part->address);

    ili9341_clear(0x0000);
    display_flush();
    ili9341_poweroff();
    vTaskDelay(pdMS_TO_TICKS(50));

    esp_ota_set_boot_partition(part);
    esp_restart();
  }

  void rom_delete_save() {
    draw_background();
    char message[100] = "deleting...";
    int w = strlen(message) * 20;
    int x = (WIDTH - w) / 2;
    int y = (480 - 32) / 2;
    draw_text(x, y, message, false, false, false);

    int bar_y = y + 40;
    int bar_h = 4;
    for(int n = 0; n < w; n+=4) {
      int bw = ((n+4) <= w) ? 4 : w - n;
      for(int i = 0; i < bw * bar_h; i++) buffer[i] = GUI.hl;
      ili9341_write_frame_rectangleLE(x+n, bar_y, bw, bar_h, buffer);
      display_flush();
      usleep(5000);
    }

    DIR *directory;
    struct dirent *file;
    char path[256] = "/sd/odroid/data/";

    strcat(&path[strlen(path) - 1], get_save_subdir());

    printf("\n----- %s -----\n%s\n", __func__, path);

    directory = opendir(path);
    while ((file = readdir(directory)) != NULL) {
      char tmp[256] = "";
      char file_to_delete[256] = "";
      strcat(tmp, file->d_name);
      sprintf(file_to_delete, "%s/%s", path, file->d_name);
      tmp[strlen(tmp)-4] = '\0';
      if(strcmp(ROM.name, tmp) == 0) {
        //printf("\nDIRECTORIES[STEP]:%s ROM.name:%s tmp:%s",DIRECTORIES[STEP], ROM.name, tmp);
        struct stat st;
        if (stat(file_to_delete, &st) == 0) {
          unlink(file_to_delete);
          LAUNCHER = false;
          draw_background();
          draw_systems();
          draw_text(16,4,EMULATORS[STEP],false,true, false);
          STEP == 0 ? draw_settings() : 
            STEP == 1 ? get_favorites() : 
            STEP == 2 ? get_recents() : 
            get_files();
        }
      }
    }
    //closedir(path);
  }
//}#pragma endregion ROM Options

//{#pragma region Browser
  /*
   * draw_carousel_screen - Draw the system carousel (no ROM list)
   * Shows: system icons ribbon + selected system name + battery/speaker
   */
  void draw_carousel_screen() {
    draw_background();
    restore_layout();
  }

  /*
   * draw_browser_header - Draw the top bar of the ROM browser
   */
  void draw_browser_header() {
    draw_mask(0, 0, WIDTH, 48);
    draw_text(8, 4, EMULATORS[STEP], false, true, false);

    /* Page info on the right */
    char info[20];
    sprintf(info, "(%d/%d)", ROMS.offset + BROWSER_SEL + 1, ROMS.total);
    int w = 0;
    for (const char *p = info; *p; p++) w += (*p == ' ') ? 10 : 20;
    draw_text(WIDTH - 20 - w, 4, info, false, false, false);
  }

  /*
   * papp_preview_free - Free cached PAPP preview image
   */
  static void papp_preview_free(void) {
    if (papp_preview_buf) { free(papp_preview_buf); papp_preview_buf = NULL; }
    papp_preview_w = 0;
    papp_preview_h = 0;
    papp_preview_name[0] = '\0';
  }

  /*
   * draw_papp_preview - Load and display a PNG preview for the selected PAPP file.
   * Looks for <ROM.path>/<basename>.png alongside the .papp file.
   * Scales to fit 300x300, displayed at right-center of browser area.
   */
  static void draw_papp_preview(void) {
    if (!ext_eq(EXTENSIONS[STEP], "papp") || !ROM.ready) return;

    /* Check if already cached for this file */
    if (!papp_preview_buf || strcmp(papp_preview_name, ROM.art) != 0) {
      /* Free old */
      papp_preview_free();

      /* Build PNG path: same folder, same base name, .png extension */
      char png_path[512];
      snprintf(png_path, sizeof(png_path), "%s/%s.png", ROM.path, ROM.art);

      pngObject png = {0};
      if (!loadPngFromFileRaw(png_path, &png, true, true)) return;
      if (png.w == 0 || png.h == 0 || !png.data) return;

      uint16_t *src = (uint16_t *)png.data;
      int sw = png.w, sh = png.h;

      /* Scale to fit 300x300 maintaining aspect ratio */
      int dw, dh;
      if (sw >= sh) {
        dw = 300; dh = (sh * 300) / sw;
      } else {
        dh = 300; dw = (sw * 300) / sh;
      }
      if (dw < 1) dw = 1;
      if (dh < 1) dh = 1;

      papp_preview_buf = (uint16_t *)heap_caps_malloc(dw * dh * 2, MALLOC_CAP_SPIRAM);
      if (!papp_preview_buf) { free(src); return; }

      /* Nearest-neighbor scale with byte-swap + R<->B swap for MIPI panel */
      for (int y = 0; y < dh; y++) {
        int sy = (y * sh) / dh;
        for (int x = 0; x < dw; x++) {
          int sx = (x * sw) / dw;
          uint16_t p = src[sy * sw + sx];
          uint16_t bs = (p >> 8) | (p << 8);
          uint16_t fixed = ((bs & 0x1F) << 11) | (bs & 0x07E0) | ((bs >> 11) & 0x1F);
          papp_preview_buf[y * dw + x] = fixed;
        }
      }
      free(src);

      papp_preview_w = dw;
      papp_preview_h = dh;
      strncpy(papp_preview_name, ROM.art, sizeof(papp_preview_name) - 1);
      papp_preview_name[sizeof(papp_preview_name) - 1] = '\0';
    }

    /* Blit: right side, vertically centered in browser area */
    int preview_x = WIDTH - papp_preview_w - 16;
    int browser_y_start = 44;
    int browser_h = BROWSER_LIMIT * 44;
    int preview_y = browser_y_start + (browser_h - papp_preview_h) / 2;
    if (preview_y < browser_y_start) preview_y = browser_y_start;

    /* Clear the preview zone first (removes old preview residue) */
    int clear_x = WIDTH - 300 - 16;
    int clear_y = browser_y_start + (browser_h - 300) / 2;
    if (clear_y < browser_y_start) clear_y = browser_y_start;
    int clear_h = 300;
    if (clear_y + clear_h > browser_y_start + browser_h)
      clear_h = browser_y_start + browser_h - clear_y;
    draw_mask(clear_x, clear_y, 316, clear_h);

    /* Blit in strips that fit buffer[64000] */
    int max_rows = 64000 / papp_preview_w;
    if (max_rows < 1) max_rows = 1;
    for (int row = 0; row < papp_preview_h; row += max_rows) {
      int rows = (row + max_rows <= papp_preview_h) ? max_rows : (papp_preview_h - row);
      int count = rows * papp_preview_w;
      for (int i = 0; i < count; i++)
        buffer[i] = papp_preview_buf[row * papp_preview_w + i];
      ili9341_write_frame_rectangleLE(preview_x, preview_y + row, papp_preview_w, rows, buffer);
    }
  }

  /*
   * draw_browser_list - Draw the ROM list in browser mode (full screen)
   * Uses BROWSER_LIMIT items per page, starting from ROMS.offset.
   * The first item (index 0 in the visible list) is the selected one.
   */
  void draw_browser_list() {
    int x = 16;
    int y_start = 44;
    int row_h = 44;

    /* Clear the list area in safe strips */
    for (int s = 0; s < BROWSER_LIMIT; s++) {
      draw_mask(0, y_start + s * row_h, WIDTH, row_h);
    }
    /* Clear any remaining pixels below last row */
    draw_mask(0, y_start + BROWSER_LIMIT * row_h, WIDTH, 8);

    int limit = (ROMS.total - ROMS.offset) < BROWSER_LIMIT ?
      (ROMS.total - ROMS.offset) : BROWSER_LIMIT;

    for (int n = 0; n < limit; n++) {
      int y = y_start + n * row_h;
      bool selected = (n == BROWSER_SEL);

      /* Draw small system icon for non-directories */
      bool is_dir = strcmp(&FILES[n][strlen(FILES[n]) - 3], "dir") == 0;
      if (is_dir) {
        draw_folder(x, y, selected);
      } else {
        draw_media(x, y, selected, -1);
      }

      /* Draw filename (without extension) */
      draw_text(x + 40, y + 6, FILES[n], true, selected, false);

      /* Draw star for favorites (non-directory items only) */
      if (!is_dir && STEP >= 3) {
        char fav_path[512];
        snprintf(fav_path, sizeof(fav_path), "%s/%s", ROM.path, FILES[n]);
        if (check_favorite_cached(fav_path)) {
          draw_star(752, y + 6);
        }
      }

      /* Set ROM info for the selected item */
      if (selected) {
        strcpy(ROM.name, FILES[n]);
        strcpy(ROM.art, remove_ext(ROM.name, '.', '/'));
        /* Extract file extension into ROM.ext */
        char *dot = strrchr(FILES[n], '.');
        if (dot && dot != FILES[n]) {
          strcpy(ROM.ext, dot + 1);
        } else {
          strcpy(ROM.ext, EXTENSIONS[STEP]);
        }
        ROM.ready = true;
      }
    }

    /* Draw scrollbar */
    if (ROMS.total > BROWSER_LIMIT) {
      int bar_x = 788;
      int bar_h = BROWSER_LIMIT * row_h;
      int thumb_h = (BROWSER_LIMIT * bar_h) / ROMS.total;
      if (thumb_h < 8) thumb_h = 8;
      int thumb_y = y_start + (ROMS.offset * (bar_h - thumb_h)) / (ROMS.total - 1);

      /* Draw track */
      for (int i = 0; i < bar_h; i++) buffer[i] = GUI.bg;
      /* darken track slightly â€” use fg at low intensity */
      draw_mask(bar_x, y_start, 8, bar_h);

      /* Draw thumb */
      for (int i = 0; i < thumb_h * 8; i++) buffer[i] = GUI.fg;
      ili9341_write_frame_rectangleLE(bar_x, thumb_y, 8, thumb_h, buffer);
    }

    /* Draw PAPP preview image if available */
    draw_papp_preview();
  }

  /*
   * draw_browser_row - Redraw a single row in the browser list.
   * n = row index within visible page (0..BROWSER_LIMIT-1)
   */
  void draw_browser_row(int n) {
    int x = 16;
    int y_start = 44;
    int row_h = 44;

    int limit = (ROMS.total - ROMS.offset) < BROWSER_LIMIT ?
      (ROMS.total - ROMS.offset) : BROWSER_LIMIT;
    if (n < 0 || n >= limit) return;

    int y = y_start + n * row_h;
    bool selected = (n == BROWSER_SEL);

    /* Clear this row */
    draw_mask(0, y, WIDTH, row_h);

    /* Draw icon */
    bool is_dir = strcmp(&FILES[n][strlen(FILES[n]) - 3], "dir") == 0;
    if (is_dir) {
      draw_folder(x, y, selected);
    } else {
      draw_media(x, y, selected, -1);
    }

    /* Draw filename */
    draw_text(x + 40, y + 6, FILES[n], true, selected, false);

    /* Draw star for favorites (non-directory items only) */
    if (!is_dir && STEP >= 3) {
      char fav_path[512];
      snprintf(fav_path, sizeof(fav_path), "%s/%s", ROM.path, FILES[n]);
      if (check_favorite_cached(fav_path)) {
        draw_star(752, y + 6);
      }
    }

    /* Update ROM info if selected */
    if (selected) {
      strcpy(ROM.name, FILES[n]);
      strcpy(ROM.art, remove_ext(ROM.name, '.', '/'));
      /* Extract file extension into ROM.ext */
      char *dot = strrchr(FILES[n], '.');
      if (dot && dot != FILES[n]) {
        strcpy(ROM.ext, dot + 1);
      } else {
        strcpy(ROM.ext, EXTENSIONS[STEP]);
      }
      ROM.ready = true;
    }
  }

  /*
   * draw_browser_scrollbar - Redraw just the scrollbar area
   */
  void draw_browser_scrollbar() {
    int y_start = 44;
    int row_h = 44;
    if (ROMS.total > BROWSER_LIMIT) {
      int bar_x = 788;
      int bar_h = BROWSER_LIMIT * row_h;
      int thumb_h = (BROWSER_LIMIT * bar_h) / ROMS.total;
      if (thumb_h < 8) thumb_h = 8;
      int thumb_y = y_start + (ROMS.offset * (bar_h - thumb_h)) / (ROMS.total - 1);
      draw_mask(bar_x, y_start, 8, bar_h);
      for (int i = 0; i < thumb_h * 8; i++) buffer[i] = GUI.fg;
      ili9341_write_frame_rectangleLE(bar_x, thumb_y, 8, thumb_h, buffer);
    }
  }

  /*
   * browser_partial_update - Redraw only the old/new rows + header
   * oldSel = previous BROWSER_SEL, newSel = new BROWSER_SEL
   */
  void browser_partial_update(int oldSel, int newSel) {
    if (ext_eq(EXTENSIONS[STEP], "papp")) {
      /* Full redraw for PAPP: preview clear wipes filenames, must restore all */
      draw_browser_list();
    } else {
      draw_browser_row(oldSel);
      draw_browser_row(newSel);
    }
    draw_browser_header();
    display_flush();
  }

  /*
   * draw_browser_screen - Full redraw of the ROM browser
   */
  void draw_browser_screen() {
    clear_screen();
    draw_browser_header();
    draw_browser_list();
    display_flush();
  }

  /*
   * enter_browser - Switch from carousel to ROM browser
   */
  void enter_browser() {
    BROWSER = true;
    ROMS.offset = 0;
    BROWSER_SEL = 0;
    ROMS.limit = BROWSER_LIMIT;
    folder_path[0] = 0;
    FOLDER = false;

    load_favorites_cache();

    clear_screen();
    draw_browser_header();

    /* Count and load files */
    count_files();
    if (ROMS.total > 0) {
      seek_files();
      draw_browser_screen();
    } else {
      draw_browser_header();
      char msg[64];
      sprintf(msg, "no %s roms found", DIRECTORIES[STEP]);
      int cx = (WIDTH - strlen(msg) * 20) / 2;
      draw_text(cx, 224, msg, false, false, false);
      display_flush();
    }
  }

  /*
   * leave_browser - Return from ROM browser to carousel
   */
  void leave_browser() {
    BROWSER = false;
    LAUNCHER = false;
    FOLDER = false;
    ROMS.limit = 8;  /* restore original limit */
    ROMS.offset = 0;
    BROWSER_SEL = 0;
    folder_path[0] = 0;
    papp_preview_free();
    free_sorted_files();
    draw_carousel_screen();
  }

  /*
   * browser_seek_and_draw - Reload current page of files and redraw browser
   */
  void browser_seek_and_draw() {
    seek_files();
    if (ROMS.total > 0) {
      draw_browser_header();
      draw_browser_list();
      display_flush();
    }
  }
//}#pragma endregion Browser

//{#pragma region Launcher
  static void launcher() {

  static uint16_t last_wizard_vid = 0, last_wizard_pid = 0;

  //{#pragma region Gamepad
    while (true) {
      odroid_input_gamepad_read(&gamepad);

      /* Auto-map wizard: trigger when a new unknown USB controller is plugged in */
      {
        uint16_t vid = 0, pid = 0;
        gamepad_get_vid_pid(&vid, &pid);
        if (vid != 0 && pid != 0 &&
            (vid != last_wizard_vid || pid != last_wizard_pid)) {
          /* New controller detected — check if it has a mapping */
          if (!odroid_input_usb_map_exists()) {
            last_wizard_vid = vid;
            last_wizard_pid = pid;
            run_map_controller_wizard();
            draw_background();
            restore_layout();
            display_flush();
            continue;  /* re-read gamepad after wizard */
          }
          last_wizard_vid = vid;
          last_wizard_pid = pid;
        }
      }

      /* ============================================================
       *  CAROUSEL MODE  (system selection)
       * ============================================================ */
      if (!BROWSER && !LAUNCHER) {

        /* --- LEFT: previous system --- */
        if (gamepad.values[ODROID_INPUT_LEFT]) {
          if (STEP != 0 || SETTING == 0) {
            STEP--;
            if (STEP < 0) STEP = COUNT - 1;
            ROMS.offset = 0;
            animate(-1);
          } else if (STEP == 0) {
            /* Settings adjustments: LEFT decreases */
            if (SETTING == 1 && VOLUME > 0) { VOLUME--; set_volume(); usleep(200000); }
            if (SETTING == 2 && BRIGHTNESS > 0) { BRIGHTNESS--; set_brightness(); usleep(200000); }
          }
          usleep(100000);
        }

        /* --- RIGHT: next system --- */
        if (gamepad.values[ODROID_INPUT_RIGHT]) {
          if (STEP != 0 || SETTING == 0) {
            STEP++;
            if (STEP > COUNT - 1) STEP = 0;
            ROMS.offset = 0;
            animate(1);
          } else if (STEP == 0) {
            if (SETTING == 1 && VOLUME < 8) { VOLUME++; set_volume(); usleep(200000); }
            if (SETTING == 2 && BRIGHTNESS < (BRIGHTNESS_COUNT-1)) { BRIGHTNESS++; set_brightness(); usleep(200000); }
          }
          usleep(100000);
        }

        /* --- UP/DOWN: settings navigation when on STEP 0 --- */
        if (gamepad.values[ODROID_INPUT_UP]) {
          if (STEP == 0) {
            SETTING--;
            if (SETTING < 0) SETTING = 3;
            draw_settings();
          }
          usleep(200000);
        }

        if (gamepad.values[ODROID_INPUT_DOWN]) {
          if (STEP == 0) {
            SETTING++;
            if (SETTING > 3) SETTING = 0;
            draw_settings();
          }
          usleep(200000);
        }

        /* --- A: enter ROM browser / settings action --- */
        if (gamepad.values[ODROID_INPUT_A]) {
          if (STEP == 0) {
            if (SETTING == 0) {
              delete_recents();
            }
            if (SETTING == 3) {
              run_map_controller_wizard();
              draw_background();
              draw_settings();
            }
          } else if (STEP >= 3) {
            /* Enter ROM browser for emulator systems */
            enter_browser();
          } else if (STEP == 1) {
            /* Favorites browser */
            BROWSER = true;
            ROMS.offset = 0;
            BROWSER_SEL = 0;
            ROMS.limit = BROWSER_LIMIT;
            clear_screen();
            draw_text(4, 4, EMULATORS[STEP], false, true, false);
            get_favorites();
          } else if (STEP == 2) {
            /* Recent browser */
            BROWSER = true;
            ROMS.offset = 0;
            BROWSER_SEL = 0;
            ROMS.limit = BROWSER_LIMIT;
            clear_screen();
            draw_text(4, 4, EMULATORS[STEP], false, true, false);
            get_recents();
          }
          debounce(ODROID_INPUT_A);
        }

        /* --- B: no action in carousel mode --- */
        if (gamepad.values[ODROID_INPUT_B]) {
          debounce(ODROID_INPUT_B);
        }

      /* ============================================================
       *  ROM BROWSER MODE
       * ============================================================ */
      } else if (BROWSER && !LAUNCHER) {

        /* --- UP: move cursor up, scroll when at top --- */
        if (gamepad.values[ODROID_INPUT_UP]) {
          if (ROMS.total > 0) {
            if (BROWSER_SEL > 0) {
              /* Cursor moves up within the visible page â€” partial redraw */
              int oldSel = BROWSER_SEL;
              BROWSER_SEL--;
              if (STEP >= 3) {
                browser_partial_update(oldSel, BROWSER_SEL);
              } else {
                char **list = STEP == 1 ? FAVORITES : RECENTS;
                favrecent_partial_update(oldSel, BROWSER_SEL, list);
              }
            } else if (ROMS.offset > 0) {
              /* At top of page, scroll up by one â€” highlight stays at top */
              ROMS.offset--;
              if (STEP >= 3) {
                browser_seek_and_draw();
              } else {
                STEP == 1 ? process_favorites() : process_recents();
              }
            } else {
              /* At very first item â€” wrap to last */
              ROMS.offset = ROMS.total > BROWSER_LIMIT ? ROMS.total - BROWSER_LIMIT : 0;
              int visible = ROMS.total - ROMS.offset;
              BROWSER_SEL = visible - 1;
              if (STEP >= 3) {
                browser_seek_and_draw();
              } else {
                STEP == 1 ? process_favorites() : process_recents();
              }
            }
          }
          usleep(150000);
        }

        /* --- DOWN: move cursor down, scroll when at bottom --- */
        if (gamepad.values[ODROID_INPUT_DOWN]) {
          if (ROMS.total > 0) {
            int visible = (ROMS.total - ROMS.offset) < BROWSER_LIMIT ?
              (ROMS.total - ROMS.offset) : BROWSER_LIMIT;
            if (BROWSER_SEL < visible - 1) {
              /* Cursor moves down within the visible page â€” partial redraw */
              int oldSel = BROWSER_SEL;
              BROWSER_SEL++;
              if (STEP >= 3) {
                browser_partial_update(oldSel, BROWSER_SEL);
              } else {
                char **list = STEP == 1 ? FAVORITES : RECENTS;
                favrecent_partial_update(oldSel, BROWSER_SEL, list);
              }
            } else if (ROMS.offset + BROWSER_LIMIT < ROMS.total) {
              /* At bottom of page, scroll down by one â€” highlight stays at bottom */
              ROMS.offset++;
              if (STEP >= 3) {
                browser_seek_and_draw();
              } else {
                STEP == 1 ? process_favorites() : process_recents();
              }
            } else {
              /* At very last item â€” wrap to first */
              ROMS.offset = 0;
              BROWSER_SEL = 0;
              if (STEP >= 3) {
                browser_seek_and_draw();
              } else {
                STEP == 1 ? process_favorites() : process_recents();
              }
            }
          }
          usleep(150000);
        }

        /* --- LEFT: page up --- */
        if (gamepad.values[ODROID_INPUT_LEFT]) {
          if (ROMS.total > 0) {
            ROMS.offset -= BROWSER_LIMIT;
            if (ROMS.offset < 0) ROMS.offset = 0;
            BROWSER_SEL = 0;
            if (STEP >= 3) {
              browser_seek_and_draw();
            } else {
              STEP == 1 ? process_favorites() : process_recents();
            }
          }
          usleep(200000);
        }

        /* --- RIGHT: page down --- */
        if (gamepad.values[ODROID_INPUT_RIGHT]) {
          if (ROMS.total > 0) {
            ROMS.offset += BROWSER_LIMIT;
            if (ROMS.offset >= ROMS.total) ROMS.offset = ROMS.total - 1;
            BROWSER_SEL = 0;
            if (STEP >= 3) {
              browser_seek_and_draw();
            } else {
              STEP == 1 ? process_favorites() : process_recents();
            }
          }
          usleep(200000);
        }

        /* --- A: select ROM / enter folder --- */
        if (gamepad.values[ODROID_INPUT_A]) {
          if (ROMS.total > 0 && ROM.ready) {
            char file_to_load[256] = "";
            sprintf(file_to_load, "%s/%s", ROM.path, ROM.name);
            bool is_directory = strcmp(&file_to_load[strlen(file_to_load) - 3], "dir") == 0;

            if (is_directory) {
              /* Enter subdirectory */
              FOLDER = true;
              PREVIOUS = ROMS.offset;
              ROMS.offset = 0;
              BROWSER_SEL = 0;
              sprintf(folder_path, "/%s", ROM.name);
              folder_path[strlen(folder_path) - (strlen(EXTENSIONS[STEP]) == 3 ? 4 : 3)] = 0;
              count_files();
              if (ROMS.total > 0) {
                seek_files();
                draw_browser_screen();
              } else {
                draw_browser_header();
                char msg[] = "empty folder";
                int cx = (WIDTH - strlen(msg) * 20) / 2;
                draw_text(cx, 224, msg, false, false, false);
                display_flush();
              }
            } else {
              /* Show ROM launch options */
              LAUNCHER = true;
              OPTION = 0;
              odroid_settings_RomFilePath_set(file_to_load);
              draw_launcher();
            }
          }
          debounce(ODROID_INPUT_A);
        }

        /* --- B: go back (folder -> browser, browser -> carousel) --- */
        if (gamepad.values[ODROID_INPUT_B]) {
          if (FOLDER) {
            /* Exit subfolder, return to parent */
            FOLDER = false;
            ROMS.offset = PREVIOUS;
            BROWSER_SEL = 0;
            PREVIOUS = 0;
            folder_path[0] = 0;
            count_files();
            if (ROMS.total > 0) {
              seek_files();
              draw_browser_screen();
            }
          } else {
            /* Exit browser, return to carousel */
            leave_browser();
          }
          debounce(ODROID_INPUT_B);
        }

        /* --- X/Y: search keyboard (emulator browser only, STEP >= 3) --- */
        if ((gamepad.values[ODROID_INPUT_MENU] || gamepad.values[ODROID_INPUT_Y]) && STEP >= 3 && ROMS.total > 0) {
          show_search_keyboard();
          if (gamepad.values[ODROID_INPUT_MENU]) debounce(ODROID_INPUT_MENU);
          if (gamepad.values[ODROID_INPUT_Y]) debounce(ODROID_INPUT_Y);
        }

      /* ============================================================
       *  ROM LAUNCH OPTIONS  (Play / Resume / Favorite)
       * ============================================================ */
      } else if (LAUNCHER) {

        /* UP/DOWN: navigate options */
        if (gamepad.values[ODROID_INPUT_UP]) {
          int min = SAVED ? 3 : 1;
          OPTION--;
          if (OPTION < 0) OPTION = min;
          draw_launcher_options();
          usleep(200000);
        }
        if (gamepad.values[ODROID_INPUT_DOWN]) {
          int max = SAVED ? 3 : 1;
          OPTION++;
          if (OPTION > max) OPTION = 0;
          draw_launcher_options();
          usleep(200000);
        }

        /* A: execute option */
        if (gamepad.values[ODROID_INPUT_A]) {
          char path[256] = "";
          sprintf(path, "%s/%s", ROM.path, ROM.name);
          switch (OPTION) {
            case 0:
              add_recent(path);
              SAVED ? rom_resume() : rom_run(false);
              break;
            case 1:
              SAVED ? rom_run(true) : ROM.favorite ? delete_favorite(path) : add_favorite(path);
              if (!SAVED) { load_favorites_cache(); draw_launcher_options(); }
              break;
            case 2:
              rom_delete_save();
              break;
            case 3:
              ROM.favorite ? delete_favorite(path) : add_favorite(path);
              load_favorites_cache();
              draw_launcher_options();
              break;
          }
          debounce(ODROID_INPUT_A);
        }

        /* B: back to browser */
        if (gamepad.values[ODROID_INPUT_B]) {
          LAUNCHER = false;
          if (BROWSER && ROMS.total > 0) {
            /* Return to browser */
            seek_files();
            draw_browser_screen();
          } else {
            draw_carousel_screen();
          }
          debounce(ODROID_INPUT_B);
        }
      }

      /* --- MENU: restart (disabled) --- */
      /* if (gamepad.values[ODROID_INPUT_MENU]) {
        usleep(10000);
        esp_restart();
        debounce(ODROID_INPUT_MENU);
      } */

      /* Yield to RTOS so watchdog & other tasks can run */
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  //}#pragma endregion GamePad
  }
//}#pragma endregion Launcher
