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

  #define BROWSER_LIMIT 12
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

  char** RECENTS;
  char RECENT[256] = "";

  int ROM_COUNTS[COUNT]; /* cached ROM count per system */

  char folder_path[256] = "";

  DIR *directory;
  struct dirent *file;
//}#pragma endregion Global

//{#pragma region Emulator and Directories
  char EMULATORS[COUNT][30] = {
    "SETTINGS",
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
    "ATARI LYNX"
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
    "lynx"      // handy
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
    "lnx"       // handy
  };

  int LIMIT = 6;
//}#pragma endregion Emulator and Directories

//{#pragma region Buffer
  unsigned short buffer[40000];
//}#pragma endregion Buffer

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
            printf("*** Safe mode active — state reset ***\n");
        }
    }

    // Battery
    odroid_input_battery_level_init();

    // SD
    odroid_sdcard_open("/sd");
    create_settings();

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
      } else if (STEP == 14) {
        /* OpenTyrian: standalone game, return to carousel */
        BROWSER = false;
        draw_background();
        restore_layout();
        display_flush();
      } else if (STEP >= 3) {
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
          int cx = (320 - strlen(msg) * 5) / 2;
          draw_text(cx, 120, msg, false, false, false);
          display_flush();
        }
      } else {
        /* STEP == 0 (settings) — shouldn't happen, but fall back to carousel */
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

  bool matches_rom_extension(const char *name, int step) {
    if (name[0] == '.') return false;
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    const char *ext = dot + 1;
    if (strlen(EXTENSIONS[step]) > 0 && ext_eq(ext, EXTENSIONS[step])) return true;
    // Atari 800 supports both .xex and .atr
    if (step == 16 && ext_eq(ext, "atr")) return true;
    // Spectrum supports both .z80 and .sna
    if (step == 10 && ext_eq(ext, "sna")) return true;
    // Atari 2600 supports both .a26 and .bin
    if (step == 11 && ext_eq(ext, "bin")) return true;
    return false;
  }

  int get_application (char* ext) {
    int application = 0;
    if(ext_eq(ext, "nes")) {application = 1;}  /* nofrendo */
    if(ext_eq(ext, "gb")) {application = 2;}   /* gnuboy */
    if(ext_eq(ext, "gbc")) {application = 2;}  /* gnuboy */
    if(ext_eq(ext, "sms")) {application = 3;}  /* smsplus */
    if(ext_eq(ext, "gg")) {application = 3;}   /* smsplus */
    if(ext_eq(ext, "col")) {application = 3;}  /* smsplus */
    if(ext_eq(ext, "a78")) {application = 4;}  /* prosystem */
    if(ext_eq(ext, "z80")) {application = 5;}  /* spectrum */
    if(ext_eq(ext, "sna")) {application = 5;}  /* spectrum */
    if(ext_eq(ext, "a26")) {application = 6;}  /* stella */
    if(ext_eq(ext, "bin")) {application = 6;}  /* stella */
    if(ext_eq(ext, "lnx")) {application = 7;}  /* handy */
    return application;
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
    return ROM.ext;
  }
//}#pragma endregion Helpers

//{#pragma region Debounce
  void debounce(int key) {
    draw_battery();
    draw_speaker();
    draw_contrast();
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
    /* Clamp to visible area: each char is 7px wide, screen is 320px */
    int max_chars = (310 - x) / 7;
    if (max_chars < 1) max_chars = 1;
    if (max_chars > 64) max_chars = 64;
    if(length > max_chars){length = max_chars;}
    int rows = 7;
    int cols = 5;
    for(int n = 0; n < length; n++) {
      int dx = get_letter(string[n]);
      int i = 0;
      for(int r = 0; r < (rows); r++) {
        if(string[n] != ' ') {
          for(int c = dx; c < (dx+cols); c++) {
            buffer[i] = FONT_5x7[r][c] == 0 ? GUI.bg : current ? GUI.hl : GUI.fg;
            if(remove) {buffer[i] = GUI.bg;}
            i++;
          }
        }
      }
      if(string[n] != ' ') {
        ili9341_write_frame_rectangleLE(x, y-1, cols, rows, buffer);
      }
      x+= string[n] != ' ' ? 7 : 3;
    }
  }

  /*
   * draw_text_scaled - Draw text at 2x scale (10x14 per character).
   * Each 5x7 font pixel is doubled in both X and Y.
   * Buffer usage per char: 10*14 = 140 pixels (safe).
   */
  void draw_text_scaled(short x, short y, const char *string, uint16_t color) {
    int length = strlen(string);
    if(length > 32){length = 32;}
    int rows = 7;
    int cols = 5;
    int scale = 2;
    for(int n = 0; n < length; n++) {
      int dx = get_letter(string[n]);
      if(string[n] != ' ') {
        int i = 0;
        for(int r = 0; r < rows; r++) {
          for(int sr = 0; sr < scale; sr++) {
            for(int c = dx; c < (dx+cols); c++) {
              uint16_t pixel = FONT_5x7[r][c] == 0 ? GUI.bg : color;
              for(int sc = 0; sc < scale; sc++) {
                buffer[i++] = pixel;
              }
            }
          }
        }
        ili9341_write_frame_rectangleLE(x, y, cols*scale, rows*scale, buffer);
      }
      x += string[n] != ' ' ? (cols*scale)+4 : 6;
    }
  }
//}#pragma endregion Text

//{#pragma region Mask
  void draw_mask(int x, int y, int w, int h){
    for (int i = 0; i < w * h; i++) buffer[i] = GUI.bg;
    ili9341_write_frame_rectangleLE(x, y, w, h, buffer);
  }

  void draw_background() {
    int w = 320;
    int h = 60;
    for (int i = 0; i < 4; i++) draw_mask(0, i*h, w, h);
    draw_battery();
    draw_speaker();
    draw_contrast();
  }

  /* Clear the full screen safely in strips (buffer is only 40000 elements!) */
  void clear_screen() {
    int w = 320;
    int h = 60;
    for (int i = 0; i < 4; i++) draw_mask(0, i*h, w, h);
  }
//}#pragma endregion Mask

//{#pragma region ROM Counts
  /* Lightweight scan: count ROM files per system for carousel display */
  void count_all_roms() {
    for (int e = 0; e < COUNT; e++) {
      ROM_COUNTS[e] = 0;

      if (e == 0) continue; /* settings – no count */

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
    int y = POS.y + 46;

    draw_mask(x,y-1,100,17);
    draw_text(x,y,(char *)"THEMES",false, SETTING == 0 ? true : false, false);

    y+=20;
    draw_mask(x,y-1,100,17);
    draw_text(x,y,(char *)"COLORED ICONS",false, SETTING == 1 ? true : false, false);
    draw_toggle();

    y+=20;
    draw_mask(x,y-1,100,17);
    draw_text(x,y,(char *)"VOLUME",false, SETTING == 2 ? true : false, false);

    draw_volume();

    y+=20;
    draw_mask(x,y-1,100,17);
    draw_text(x,y,(char *)"BRIGHTNESS",false, SETTING == 3 ? true : false, false);

    draw_brightness();

    y+=20;
    draw_mask(x,y-1,100,17);
    draw_text(x,y,(char *)"COVER ART",false, SETTING == 4 ? true : false, false);

    draw_cover_toggle();

    y+=20;
    draw_mask(x,y-1,100,17);
    draw_text(x,y,(char *)"CLEAR RECENTS",false, SETTING == 5 ? true : false, false);

    draw_cover_toggle();    

    /*
      BUILD
    */
    char message[100] = BUILD;
    int width = strlen(message)*5;
    int center = ceil((320)-(width))-48;
    y = 225;
    draw_text(center,y,message,false,false, false);
    display_flush();
  }
//}#pragma endregion Settings

//{#pragma region Toggle
  void draw_toggle() {
    get_toggle();
    int x = SCREEN.w - 38;
    int y = POS.y + 66;
    int w, h;

    int i = 0;
    for(h = 0; h < 9; h++) {
      for(w = 0; w < 18; w++) {
        buffer[i] = toggle[h + (COLOR*9)][w] == 0 ? 
        GUI.bg : toggle[h + (COLOR*9)][w] == WHITE ? 
        SETTING == 1 ? GUI.hl : GUI.fg : toggle[h + (COLOR*9)][w];
        i++;
      }
    }
    ili9341_write_frame_rectangleLE(x, y, 18, 9, buffer);
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
    int x = SCREEN.w - 38;
    int y = POS.y + 126;
    int w, h;

    int i = 0;
    for(h = 0; h < 9; h++) {
      for(w = 0; w < 18; w++) {
        buffer[i] = toggle[h + (COVER*9)][w] == 0 ? 
        GUI.bg : toggle[h + (COVER*9)][w] == WHITE ? 
        SETTING == 4 ? GUI.hl : GUI.fg : toggle[h + (COVER*9)][w];
        i++;    
      }
    }
    /*
        buffer[i] = toggle[h + (COVER*9)][w] == 0 ? 
        GUI.bg : toggle[h + (COVER*9)][w] == WHITE ? 
        SETTING == 4 ? GUI.hl : GUI.fg : toggle[h + (COVER*9)][w];
        i++;    
    */
    ili9341_write_frame_rectangleLE(x, y, 18, 9, buffer);
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
    int x = SCREEN.w - 120;
    int y = POS.y + 86;
    //int w = 25 * volume;
    int w, h;

    int i = 0;
    for(h = 0; h < 7; h++) {
      for(w = 0; w < 100; w++) {
        buffer[i] = (w+h)%2 == 0 ? GUI.fg : GUI.bg;
        i++;
      }
    }
    ili9341_write_frame_rectangleLE(x, y, 100, 7, buffer);

    if(volume > 0) {
      i = 0;
      for(h = 0; h < 7; h++) {
        for(w = 0; w < (12.5 * volume); w++) {
          if(SETTING == 2) {
            buffer[i] = GUI.hl;
          } else {
            buffer[i] = GUI.fg;
          }
          i++;
        }
      }
      ili9341_write_frame_rectangleLE(x, y, (12.5 * volume), 7, buffer);
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
    int x = SCREEN.w - 120;
    int y = POS.y + 106;
    int w, h;

    int i = 0;
    for(h = 0; h < 7; h++) {
      for(w = 0; w < 100; w++) {
        buffer[i] = (w+h)%2 == 0 ? GUI.fg : GUI.bg;
        i++;
      }
    }
    ili9341_write_frame_rectangleLE(x, y, 100, 7, buffer);

    //if(BRIGHTNESS > 0) {
      i = 0;
      for(h = 0; h < 7; h++) {
        for(w = 0; w < (BRIGHTNESS_COUNT * BRIGHTNESS)+BRIGHTNESS+1; w++) {
          if(SETTING == 3) {
            buffer[i] = GUI.hl;
          } else {
            buffer[i] = GUI.fg;
          }
          i++;
        }
      }
      ili9341_write_frame_rectangleLE(x, y, (BRIGHTNESS_COUNT * BRIGHTNESS)+BRIGHTNESS+1, 7, buffer);
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
    int y = POS.y + 46;
    int filled = 0;
    int count = 22;
    for(int n = USER; n < count; n++){
      if(filled < ROMS.limit) {
        draw_mask(x,y-1,100,17);
        draw_text(x,y,THEMES[n].name,false, n == USER ? true : false, false);
        y+=20;
        filled++;
      }
    }
    int slots = (ROMS.limit - filled);
    for(int n = 0; n < slots; n++) {
      draw_mask(x,y-1,100,17);
      draw_text(x,y,THEMES[n].name,false,false, false);
      y+=20;
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
        USER = 21;
        set_theme(USER);
        break;
      default :
        USER = 21;
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
    draw_mask(0,0,320,64);
    draw_systems();
    draw_text(16,16,EMULATORS[STEP], false, true, false);
    draw_themes();
  }
//}#pragma endregion Theme

//{#pragma region GUI
  void draw_systems() {
    for(int e = 0; e < COUNT; e++) {
      int i = 0;
      int x = SYSTEMS[e].x;
      int y = POS.y;
      int w = 32;
      int h = 32;
      if(x > 0 && x < 288) {
        for(int r = 0; r < 32; r++) {
          for(int c = 0; c < 32; c++) {
            switch(COLOR) {
              case 0:
                buffer[i] = (*SYSTEMS[e].system)[r][c] == WHITE ? GUI.hl : GUI.bg;
              break;
              case 1:
                //buffer[i] = (*SYSTEMS[e].system)[r][c] == WHITE ? GUI.hl : GUI.bg;
                buffer[i] = (*SYSTEMS[e].color)[r][c] == 0 ? GUI.bg : (*SYSTEMS[e].color)[r][c];
              break;
            }
            i++;
          }
        }
        ili9341_write_frame_rectangleLE(x, y, w, h, buffer);

        /* Draw ROM count below the icon (skip settings at index 0) */
        if (e > 0 && ROM_COUNTS[e] >= 0) {
          char cnt[12];
          sprintf(cnt, "(%d)", ROM_COUNTS[e]);
          int len = strlen(cnt);
          /* Center the count text under the 32px icon */
          int tx = x + (32 - len * 7) / 2;
          if (tx < 1) tx = 1;
          draw_text(tx, y + 34, cnt, false, false, false);
        }
      }
    }
  }

  /*
   * draw_system_logo - Draw a 3x scaled (96x96) version of the current
   * system's icon, centered horizontally below the icon ribbon.
   * Buffer usage: 96*96 = 9216 pixels (safe, buffer is 40000).
   */
  void draw_system_logo() {
    if (STEP == 0) return; /* no big logo for settings screen */
    int scale = 3;
    int w = 32 * scale; /* 96 */
    int h = 32 * scale; /* 96 */
    int x = (320 - w) / 2; /* 112 */
    int y = 72;
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

  void draw_folder(int x, int y, bool current) {
    int i = 0;
    for(int h = 0; h < 16; h++) {
      for(int w = 0; w < 16; w++) {
        buffer[i] = folder[h][w] == WHITE ? current ? GUI.hl : GUI.fg : GUI.bg;
        i++;
      }
    }
    ili9341_write_frame_rectangleLE(x, y, 16, 16, buffer);
  }

  void draw_media(int x, int y, bool current, int offset) {
    if(offset == -1) {offset = (STEP-3) * 16;}
    int i = 0;
    for(int h = 0; h < 16; h++) {
      for(int w = offset; w < (offset+16); w++) {
        switch(COLOR) {
          case 0:
            buffer[i] = media[h][w] == WHITE ? current ? GUI.hl : GUI.fg : GUI.bg;
          break;
          case 1:
            buffer[i] = media_color[h][w] == 0 ? GUI.bg : media_color[h][w];
            if(current) {
              buffer[i] = media_color[h+16][w] == 0 ? GUI.bg : media_color[h+16][w];
            }
          break;
        }
        /*

        */
        i++;
      }
    }
    ili9341_write_frame_rectangleLE(x, y, 16, 16, buffer);
  }

  void draw_battery() {
    #ifdef BATTERY
      odroid_input_battery_level_read(&battery_state);

      int i = 0;
      int x = SCREEN.w - 32;
      int y = 8;
      int h = 0;
      int w = 0;

      draw_mask(x,y,16,16);
      for(h = 0; h < 16; h++) {
        for(w = 0; w < 16; w++) {
          buffer[i] = battery[h][w] == WHITE ? GUI.hl : GUI.bg;
          i++;
        }
      }
      ili9341_write_frame_rectangleLE(x, y, 16, 16, buffer);

      int percentage = battery_state.percentage/10;
      x += 2;
      y += 6;
      w = percentage > 0 ? percentage > 10 ? 10 : percentage : 10;
      h = 4;
      i = 0;

      //printf("\nbattery_state.percentage:%d\n(percentage):%d\n(millivolts)%d\n", battery_state.percentage, percentage, battery_state.millivolts);

      int color[11] = {24576,24576,64288,64288,65504,65504,65504,26592,26592,26592,26592};

      int fill = color[w];
      for(int c = 0; c < h; c++) {
        for(int n = 0; n <= w; n++) {
          buffer[i] = fill;
          i++;
        }
      }
      ili9341_write_frame_rectangleLE(x, y, w, h, buffer);

      /*
      if(battery_state.millivolts > 4200) {
        i = 0;
        for(h = 0; h < 5; h++) {
          for(w = 0; w < 3; w++) {
            buffer[i] = charge[h][w] == WHITE ? WHITE : fill;
            i++;
          }
        }
        ili9341_write_frame_rectangleLE(x+4, y, 3, 5, buffer);
      }
      */
    #endif
  }

  void draw_speaker() {
    int32_t volume = get_volume();

    int i = 0;
    int x = SCREEN.w - 52;
    int y = 8;
    int h = 16;
    int w = 16;

    draw_mask(x,y,16,16);

    int dh = 0;
    switch(volume) {
      case 0:dh = 64;break;
      case 1:case 2:case 3:dh = 48;break;
      case 4:case 5:dh = 32;break;
      case 6:case 7:dh = 16;break;
      case 9:dh = 0;break;
    }
    for(h = 0; h < 16; h++) {
      for(w = 0; w < 16; w++) {
        buffer[i] = speaker[dh+h][w] == WHITE ? GUI.hl : GUI.bg;
        i++;
      }
    }
    ili9341_write_frame_rectangleLE(x, y, w, h, buffer);
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
    int x = SCREEN.w - 72;
    int y = 8;
    int h = 16;
    int w = 16;

    draw_mask(x,y,16,16);

    for(h = 0; h < 16; h++) {
      for(w = 0; w < 16; w++) {
        buffer[i] = brightness[dy+h][w] == WHITE ? GUI.hl : GUI.bg;
        i++;
      }
    }
    ili9341_write_frame_rectangleLE(x, y, w, h, buffer);
  }

  void draw_numbers() {
    int x = 296;
    int y = POS.y + 48;
    int w = 0;
    char count[10];
    sprintf(count, "(%d/%d)", (ROMS.offset+1), ROMS.total);
    for (const char *p = count; *p; p++) w += (*p == ' ') ? 3 : 7;
    x -= w;
    draw_text(x,y,count,false,false, false);
  }

  void delete_numbers() {
    int x = 296;
    int y = POS.y + 48;
    int w = 0;
    char count[10];
    sprintf(count, "(%d/%d)", (ROMS.offset+1), ROMS.total);
    for (const char *p = count; *p; p++) w += (*p == ' ') ? 3 : 7;
    x -= w;
    draw_text(x,y,count,false,false, true);
  }

  void draw_launcher() {
    draw_background();
    draw_text(16,16,EMULATORS[STEP], false, true, false);
    int i = 0;
    int x = GAP/3;
    int y = POS.y;
    int w = 32;
    int h = 32;
    for(int r = 0; r < 32; r++) {
      for(int c = 0; c < 32; c++) {
        switch(COLOR) {
          case 0:
            buffer[i] = (*SYSTEMS[STEP].system)[r][c] == WHITE ? GUI.hl : GUI.bg;
          break;
          case 1:
            //buffer[i] = (*SYSTEMS[e].system)[r][c] == WHITE ? GUI.hl : GUI.bg;
            buffer[i] = (*SYSTEMS[STEP].color)[r][c] == 0 ? GUI.bg : (*SYSTEMS[STEP].color)[r][c];
          break;
        }
        i++;
      }
    }
    ili9341_write_frame_rectangleLE(x, y, w, h, buffer);

    y += 48;
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
    draw_media(x,y-6,true,offset);
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
    int x = GAP/3 + 32;
    int y = POS.y + 48;
    int w = 5;
    int h = 5;
    int i = 0;
    int offset = 0;
    if(SAVED) {
      // resume
      i = 0;
      offset = 5;
      for(int r = 0; r < 5; r++){for(int c = 0; c < 5; c++) {
        buffer[i] = icons[r+offset][c] == WHITE ? OPTION == 0 ? GUI.hl : GUI.fg : GUI.bg;i++;
      }}
      ili9341_write_frame_rectangleLE(x, y, w, h, buffer);
      draw_text(x+10,y,(char *)"Resume",false,OPTION == 0?true:false, false);
      // restart
      i = 0;
      y+=20;
      offset = 10;
      for(int r = 0; r < 5; r++){for(int c = 0; c < 5; c++) {
        buffer[i] = icons[r+offset][c] == WHITE ? OPTION == 1 ? GUI.hl : GUI.fg : GUI.bg;i++;
      }}
      ili9341_write_frame_rectangleLE(x, y, w, h, buffer);
      draw_text(x+10,y,(char *)"Restart",false,OPTION == 1?true:false, false);
      // delete
      i = 0;
      y+=20;
      offset = 20;
      for(int r = 0; r < 5; r++){for(int c = 0; c < 5; c++) {
        buffer[i] = icons[r+offset][c] == WHITE ? OPTION == 2 ? GUI.hl : GUI.fg : GUI.bg;i++;
      }}
      ili9341_write_frame_rectangleLE(x, y, w, h, buffer);
      draw_text(x+10,y,(char *)"Delete Save",false,OPTION == 2?true:false, false);
    } else {
      // run
      i = 0;
      offset = 0;
      for(int r = 0; r < 5; r++){for(int c = 0; c < 5; c++) {
        buffer[i] = icons[r+offset][c] == WHITE ? OPTION == 0 ? GUI.hl : GUI.fg : GUI.bg;i++;
      }}
      ili9341_write_frame_rectangleLE(x, y, w, h, buffer);
      draw_text(x+10,y,(char *)"Run",false,OPTION == 0?true:false, false);
    }

    // favorites
    y+=20;
    i = 0;
    offset = ROM.favorite?40:35;
    int option = SAVED ? 3 : 1;
    draw_mask(x,y-1,80,9);
    for(int r = 0; r < 5; r++){for(int c = 0; c < 5; c++) {
      buffer[i] = icons[r+offset][c] == WHITE ? OPTION == option ? GUI.hl : GUI.fg : GUI.bg;i++;
    }}
    ili9341_write_frame_rectangleLE(x, y, w, h, buffer);
    draw_text(x+10,y,ROM.favorite?(char *)"Unfavorite":(char *)"Favorite",false,OPTION == option?true:false, false);
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
    int center = ceil((320/2)-((strlen(message)*5)/2));
    draw_text(center,134,message,false,false, false);

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
      draw_mask(0,132,320,10);
      sprintf(message, "unable to open %s directory", DIRECTORIES[STEP]);
      int center = ceil((320/2)-((strlen(message)*5)/2));
      draw_text(center,134,message,false,false, false);
      return;
    } else {
      if(directory == NULL) {
        draw_mask(0,132,320,10);
        sprintf(message, "%s directory not found", DIRECTORIES[STEP]);
        int center = ceil((320/2)-((strlen(message)*5)/2));
        draw_text(center,134,message,false,false, false);
      } else {
        /* First pass: count files */
        rewinddir(directory);
        seekdir(directory, 0);
        SEEK[0] = 0;
        struct dirent *file;
        while ((file = readdir(directory)) != NULL) {
          bool extenstion = matches_rom_extension(file->d_name, STEP);
          if(extenstion || (file->d_type == 2)) {
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
            if(extenstion || (file->d_type == 2)) {
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
      draw_mask(0,132,320,10);
      sprintf(message, "no %s roms available", DIRECTORIES[STEP]);
      int center = ceil((320/2)-((strlen(message)*5)/2));
      draw_text(center,134,message,false,false, false);
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
      int center = ceil((320/2)-((strlen(message)*5)/2));
      draw_mask(0,POS.y + 47,320,10);
      draw_mask(0,132,320,10);
      draw_text(center,134,message,false,false, false);
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
    int y = POS.y + 48;
    ROMS.page = ROMS.offset/ROMS.limit;

    for (int i = 0; i < 4; i++) draw_mask(0, y+(i*40)-6, 320, 40);
    //int limit = ROMS.total < ROMS.limit ? ROMS.total : ROMS.limit;
    int limit = (ROMS.total - ROMS.offset) <  ROMS.limit ?
      (ROMS.total - ROMS.offset) :
      ROMS.limit;

    //printf("\nlimit:%d", limit);
    for(int n = 0; n < limit; n++) {
      //printf("\n%d:%s", n, FILES[n]);
      draw_text(x+24,y,FILES[n],true,n == 0 ? true : false, false);
      bool directory = strcmp(&FILES[n][strlen(FILES[n]) - 3], "dir") == 0;
      directory ?
        draw_folder(x,y-6,n == 0 ? true : false) :
        draw_media(x,y-6,n == 0 ? true : false,-1);
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
      y+=20;
    }
    draw_numbers();
    //printf("\n---------------------\n");
  }

  void has_save_file(char *save_name) {
    SAVED = false;

    //  printf("\n----- %s -----", __func__);
    //printf("\nsave_name: %s", save_name);

    char save_dir[256] = "/sd/odroid/data/";
    strcat(&save_dir[strlen(save_dir) - 1], get_save_subdir());
    //  printf("\nsave_dir: %s", save_dir);

    char save_file[256] = "";
    sprintf(save_file, "%s/%s", save_dir, save_name);
    strcat(&save_file[strlen(save_file) - 1], ".sav");
    //printf("\nsave_file: %s", save_file);

    DIR *directory = opendir(save_dir);
    if(directory == NULL) {
      perror("opendir() error");
    } else {
      struct dirent *file;
      while ((file = readdir(directory)) != NULL) {
        char tmp[256] = "";
        strcat(tmp, file->d_name);
        tmp[strlen(tmp)-4] = '\0';
        //printf("\ntmp:%s save_name:%s", tmp, save_name);
        if(strcmp(save_name, tmp) == 0) {
          SAVED = true;
        }
      }
    }
    //printf("\n---------------------\n");
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
    int center = ceil((320/2)-((strlen(message)*5)/2));
    draw_text(center,134,message,false,false, false);

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
      int center = ceil((320/2)-((strlen(message)*5)/2));
      draw_mask(0, 20, 320, 18);
      draw_mask(0, 120, 320, 18);
      draw_text(center, 120, message, false, false, false);
    }

    display_flush();
    //  printf("\n----- %s END -----", __func__);
  }

  void draw_favorites() {
    //  printf("\n----- %s START -----", __func__);
    int x = 8;
    int y_start = 20;
    int row_h = 18;

    /* Clear all browser rows */
    for (int s = 0; s < BROWSER_LIMIT; s++) {
      draw_mask(0, y_start + s * row_h, 320, row_h);
    }
    draw_mask(0, y_start + BROWSER_LIMIT * row_h, 320, 4);

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

      draw_text(x+20, y+3, favorite, false, row == BROWSER_SEL, false);
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
      int bar_x = 314;
      int bar_h = BROWSER_LIMIT * row_h;
      int thumb_h = (BROWSER_LIMIT * bar_h) / ROMS.total;
      if (thumb_h < 4) thumb_h = 4;
      int thumb_y = y_start + (ROMS.offset * (bar_h - thumb_h)) / (ROMS.total - 1);
      draw_mask(bar_x, y_start, 4, bar_h);
      for (int i = 0; i < thumb_h * 4; i++) buffer[i] = GUI.fg;
      ili9341_write_frame_rectangleLE(bar_x, thumb_y, 4, thumb_h, buffer);
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
    int x = 8;
    int y_start = 20;
    int row_h = 18;

    int limit = (ROMS.total - ROMS.offset) < BROWSER_LIMIT ?
      (ROMS.total - ROMS.offset) : BROWSER_LIMIT;
    if (row < 0 || row >= limit) return;

    int n = ROMS.offset + row;
    int y = y_start + row * row_h;
    bool selected = (row == BROWSER_SEL);

    draw_mask(0, y, 320, row_h);

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

    draw_text(x+20, y+3, favorite, false, selected, false);
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
//}#pragma endregion Favorites

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
    int center = ceil((320/2)-((strlen(message)*5)/2));
    draw_text(center,134,message,false,false, false);

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
      int center = ceil((320/2)-((strlen(message)*5)/2));
      draw_mask(0, 20, 320, 18);
      draw_mask(0, 120, 320, 18);
      draw_text(center, 120, message, false, false, false);
    }

    display_flush();
    //  printf("\n----- %s END -----", __func__);
  }

  void draw_recents() {
    //  printf("\n----- %s START -----", __func__);
    int x = 8;
    int y_start = 20;
    int row_h = 18;

    /* Clear all browser rows */
    for (int s = 0; s < BROWSER_LIMIT; s++) {
      draw_mask(0, y_start + s * row_h, 320, row_h);
    }
    draw_mask(0, y_start + BROWSER_LIMIT * row_h, 320, 4);

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

      draw_text(x+20, y+3, favorite, false, row == BROWSER_SEL, false);
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
      int bar_x = 314;
      int bar_h = BROWSER_LIMIT * row_h;
      int thumb_h = (BROWSER_LIMIT * bar_h) / ROMS.total;
      if (thumb_h < 4) thumb_h = 4;
      int thumb_y = y_start + (ROMS.offset * (bar_h - thumb_h)) / (ROMS.total - 1);
      draw_mask(bar_x, y_start, 4, bar_h);
      for (int i = 0; i < thumb_h * 4; i++) buffer[i] = GUI.fg;
      ili9341_write_frame_rectangleLE(bar_x, thumb_y, 4, thumb_h, buffer);
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
    int width = strlen(message)*5;
    int center = ceil((320/2)-(width/2));
    int y = 118;
    draw_text(center,y,message,false,false, false);

    y+=10;
    for(int n = 0; n < (width+10); n++) {
      for(int i = 0; i < 5; i++) {
        buffer[i] = GUI.fg;
      }
      ili9341_write_frame_rectangleLE(center+n, y, 1, 5, buffer);
      usleep(10000);
    }

    draw_background();
    draw_systems();
    draw_text(16,16,EMULATORS[STEP],false,true, false);
    draw_settings();

  }  
//}#pragma endregion Recents

//{#pragma region Cover
  void get_cover() {
    preview_cover(false);
  }

  void preview_cover(bool error) {
    ROM.crc = 0;

    int bw = 112;
    int bh = 150;
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
        bw = width;
        bh = height;
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
    int x = SCREEN.w-24-bw;
    int y = POS.y+8;
    ili9341_write_frame_rectangleLE(x, y, bw, bh, buffer);

    int center = x + bw/2;
    center -= error ? 30 : 22;

    draw_text(center, y + (bh/2) - 3, error ? (char *)"NO PREVIEW" : (char *)"PREVIEW", false, false, false);

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

      int x = SCREEN.w-24-width;
      int y = POS.y+8;

      if (width<=320 && height<=240) {
        uint16_t *img = (uint16_t*)heap_caps_malloc(width*height*2, MALLOC_CAP_SPIRAM);
        fread(img, 2, width*height, f);
        ili9341_write_frame_rectangleLE(x,y, width, height, img);
        heap_caps_free(img);
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
    int MAX = 160 + (COUNT * GAP);
    for (int n = 0; n < COUNT; n++) {
      if (SYSTEMS[n].x > COUNT * GAP - 64) {
        SYSTEMS[n].x -= MAX;
      }
      if (SYSTEMS[n].x <= (-32 - (COUNT / 2) * 48)) {
        SYSTEMS[n].x += MAX;
      }
    }
  }

  void animate(int dir) {
    delete_numbers();
    draw_mask(0,0,SCREEN.w - 56,32);
    draw_text(16,16,EMULATORS[STEP], false, true, false);
    draw_contrast();

    int y = POS.y + 46;
    for (int i = 0; i < 4; i++) draw_mask(0, y+(i*40)-6, 320, 40);
    int sx[4][13] = {
      {8,8,4,4,4,3,3,3,3,2,2,2,2}, // 48
      {30,26,20,20,18,18,16,16,12,12,8,8,4} // 208 30+26+20+20+18+18+16+16+12+12+8+8+4
    };
    for(int i = 0; i < 13; i++) {
      if(dir == -1) {
        // LEFT
        for(int e = 0; e < COUNT; e++) {
          SYSTEMS[e].x += STEP != COUNT - 1 ?
            STEP == (e-1) ? sx[1][i] : sx[0][i] :
            e == 0 ? sx[1][i] : sx[0][i] ;
        }
      } else {
        // RIGHT
        for(int e = 0; e < COUNT; e++) {
          SYSTEMS[e].x -= STEP == e ? sx[1][i] : sx[0][i];
        }
      }
      draw_mask(0,32,320,42);
      draw_systems();
      display_flush();
      usleep(20000);
    }
    /* In carousel mode, show hint instead of auto-loading file list */
    if (STEP == 0) {
      draw_settings();
    } else {
      draw_system_logo();
      char hint[] = "press a to browse";
      int cx = (320 - strlen(hint) * 14) / 2;
      draw_text_scaled(cx, 175, hint, GUI.fg);
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
    draw_text(16,16,EMULATORS[STEP],false,true, false);

    /* In carousel mode, show a hint instead of auto-loading ROM list */
    if (!BROWSER) {
      if (STEP == 0) {
        draw_settings();
      } else {
        /* Show large system logo + hint */
        draw_system_logo();
        const char *hint = STEP == 14 ? "press a to play" : "press a to browse";
        int cx = (320 - strlen(hint) * 14) / 2;
        draw_text_scaled(cx, 175, hint, GUI.fg);
      }
    } else {
      STEP == 0 ? draw_settings() :
        STEP == 1 ? get_favorites() :
        STEP == 2 ? get_recents() :
        get_files();
    }
    int MAX = 160+(COUNT*GAP);
    for(int n = 0; n < COUNT; n++) {
      if(SYSTEMS[n].x > COUNT*GAP-64) {
        SYSTEMS[n].x -= MAX;
      }
      if(SYSTEMS[n].x <= (-32 - (COUNT/2) * 48)) {//-32 - ((14/2)*48)
        SYSTEMS[n].x += MAX;
      }
    }
    display_flush();
  }
//}#pragma endregion Animations

//{#pragma region Boot Screens
  /*───────────────────────────────────────────────────────────────
    load_bmp_logo() — Try to load /sd/boot_logo.bmp into buffer[].
    Supports 24-bit uncompressed BMP, max 320×240, w*h ≤ 40000.
    Returns 1 on success (buffer filled with RGB565, w/h/x/y set).
    Returns 0 on failure (file missing, bad format, too large).
  ───────────────────────────────────────────────────────────────*/
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
    if (bmp_w <= 0 || bmp_w > 320)               { fclose(f); return 0; }
    if (abs_h <= 0 || abs_h > 240)               { fclose(f); return 0; }
    if ((int)bmp_w * abs_h > 40000)              { fclose(f); return 0; }

    /* BMP rows are padded to 4-byte boundaries */
    int row_bytes = bmp_w * 3;
    int row_pad   = (4 - (row_bytes % 4)) % 4;
    int row_total = row_bytes + row_pad;

    /* Seek to pixel data */
    fseek(f, data_offset, SEEK_SET);

    /* Read row-by-row, converting BGR888 → RGB565 into buffer[] */
    uint8_t row_buf[960 + 4];  /* max 320*3 = 960 bytes per row */
    for (int r = 0; r < abs_h; r++) {
      int dest_row = flip ? (abs_h - 1 - r) : r;
      if (fread(row_buf, 1, row_total, f) != (size_t)row_total) {
        fclose(f); return 0;
      }
      for (int c = 0; c < bmp_w; c++) {
        uint8_t b = row_buf[c * 3 + 0];
        uint8_t g = row_buf[c * 3 + 1];
        uint8_t rd = row_buf[c * 3 + 2];
        /* RGB888 → RGB565 */
        buffer[dest_row * bmp_w + c] =
            ((rd >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
      }
    }
    fclose(f);

    *out_w = bmp_w;
    *out_h = abs_h;
    *out_x = (320 - bmp_w) / 2;   /* center on 320×240 screen */
    *out_y = (240 - abs_h) / 2;
    return 1;
  }

  /*── Helper: render the built-in logo3d into buffer[] and blit ──*/
  static void draw_builtin_logo(void)
  {
    int w = 280, h = 100;
    int x = (SCREEN.w - w) / 2;
    int y = 28;

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

    int i = 0;
    for(int r = 0; r < h; r++) {
      for(int c = 0; c < w; c++) {
        uint16_t px = logo3d[r][c];
        if (px == 0) {
          buffer[i] = GUI.bg;
        } else if (px == 65535) {
          buffer[i] = GUI.hl;
        } else if (px == 33808) {
          buffer[i] = mid_color;
        } else {
          buffer[i] = GUI.fg;
        }
        i++;
      }
    }
    ili9341_write_frame_rectangleLE(x, y, w, h, buffer);
  }

  /*── Helper: wait up to ms milliseconds, return early if A pressed ──*/
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

    /* ── Phase 1: main logo (SD BMP or built-in), 5 sec, A to skip ── */
    int w, h, x, y;
    int from_sd = load_bmp_logo(&w, &h, &x, &y);

    if (from_sd) {
      /* BMP loaded from SD card — buffer[] already has RGB565 pixels */
      ili9341_write_frame_rectangleLE(x, y, w, h, buffer);
    } else {
      draw_builtin_logo();
    }

    /* BUILD string at bottom */
    char message[100] = BUILD;
    int width = strlen(message)*5;
    int center = ceil((320)-(width))-48;
    y = 225;
    draw_text(center,y,message,false,false, false);
    display_flush();

    wait_or_button(5000);

    /* ── Phase 2: built-in logo + credit, 2 seconds ── */
    draw_background();
    draw_builtin_logo();

    /* Credit text below the logo */
    {
      char credit[] = "Done by Claude Opus 4.6";
      int cw = strlen(credit) * 7;
      int cx = (320 - cw) / 2;
      draw_text(cx, 140, credit, false, false, false);
    }
    display_flush();

    sleep(2);
    draw_background();
    display_flush();
  }

  void boot() {
    draw_background();
    char message[100] = "retro esp32";
    int width = strlen(message)*5;
    int center = ceil((320/2)-(width/2));
    int y = 118;
    draw_text(center,y,message,false,false, false);

    y+=10;
    for(int n = 0; n < (width+10); n++) {
      for(int i = 0; i < 5; i++) {
        buffer[i] = GUI.fg;
      }
      ili9341_write_frame_rectangleLE(center+n, y, 1, 5, buffer);
      display_flush();
      usleep(10000);
    }
  }


  void restart() {
    draw_background();

    char message[100] = "restarting";
    int h = 5;
    int w = strlen(message)*h;
    int x = (SCREEN.w/2)-(w/2);
    int y = (SCREEN.h/2)-(h/2);
    draw_text(x,y,message,false,false, false);

    y+=10;
    for(int n = 0; n < (w+10); n++) {
      for(int i = 0; i < 5; i++) {
        buffer[i] = GUI.fg;
      }
      ili9341_write_frame_rectangleLE(x+n, y, 1, 5, buffer);
      display_flush();
      usleep(10000);
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

    int h = 5;
    int w = strlen(message)*h;
    int x = (SCREEN.w/2)-(w/2);
    int y = (SCREEN.h/2)-(h/2);
    draw_text(x,y,message,false,false, false);
    y+=10;
    for(int n = 0; n < (w+10); n++) {
      for(int i = 0; i < 5; i++) {
        buffer[i] = GUI.fg;
      }
      ili9341_write_frame_rectangleLE(x+n, y, 1, 5, buffer);
      display_flush();
      usleep(10000);
    }

    /* Build the full ROM path */
    char rom_path[256] = "";
    sprintf(rom_path, "%s/%s", ROM.path, ROM.name);

    odroid_settings_DataSlot_set(resume ? 1 : 0);
    if (resume) {
      odroid_settings_StartAction_set(ODROID_START_ACTION_RESTART);
    }

    /* Launch the correct emulator based on file extension */
    int app = get_application(ROM.ext);
    printf("rom_run: ROM.name='%s' ROM.path='%s' ROM.ext='%s' app=%d\n",
           ROM.name, ROM.path, ROM.ext, app);
    switch (app) {
      case 1: /* NES */
        nofrendo_run(rom_path);
        break;
      case 2: /* GB / GBC */
        gnuboy_run(rom_path);
        break;
      case 3: /* SMS / GG / COL */
        smsplus_run(rom_path);
        break;
      case 4: /* Atari 7800 */
        prosystem_run(rom_path);
        break;
      case 5: /* ZX Spectrum */
        spectrum_run(rom_path);
        break;
      case 6: /* Atari 2600 */
        stella_run(rom_path);
        break;
      case 7: /* Atari Lynx */
        handy_run(rom_path);
        break;
      default:
        printf("rom_run: unsupported ROM type: %s\n", ROM.ext);
        break;
    }

    /* Emulator returned — go back to the ROM browser */
    printf("rom_run: emulator returned, restoring browser\n");
    ili9341_write_frame_nes(NULL, NULL, true);  /* clear display */
    get_restore_states();

    LAUNCHER = false;
    if (BROWSER && ROMS.total > 0) {
        seek_files();
        draw_browser_screen();
    } else {
        draw_carousel_screen();
    }
  }

  void rom_resume() {
    rom_run(true);
  }

  void rom_delete_save() {
    draw_background();
    char message[100] = "deleting...";
    int width = strlen(message)*5;
    int center = ceil((320/2)-(width/2));
    int y = 118;
    draw_text(center,y,message,false,false, false);

    y+=10;
    for(int n = 0; n < (width+10); n++) {
      for(int i = 0; i < 5; i++) {
        buffer[i] = GUI.fg;
      }
      ili9341_write_frame_rectangleLE(center+n, y, 1, 5, buffer);
      display_flush();
      usleep(10000);
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
          draw_text(16,16,EMULATORS[STEP],false,true, false);
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
    draw_battery();
    draw_speaker();
    draw_contrast();
    display_flush();
  }

  /*
   * draw_browser_header - Draw the top bar of the ROM browser
   */
  void draw_browser_header() {
    draw_mask(0, 0, 320, 16);
    draw_text(4, 4, EMULATORS[STEP], false, true, false);

    /* Page info on the right */
    char info[20];
    sprintf(info, "(%d/%d)", ROMS.offset + BROWSER_SEL + 1, ROMS.total);
    int w = 0;
    for (const char *p = info; *p; p++) w += (*p == ' ') ? 3 : 7;
    draw_text(311 - w, 4, info, false, false, false);
  }

  /*
   * draw_browser_list - Draw the ROM list in browser mode (full screen)
   * Uses BROWSER_LIMIT items per page, starting from ROMS.offset.
   * The first item (index 0 in the visible list) is the selected one.
   */
  void draw_browser_list() {
    int x = 8;
    int y_start = 20;
    int row_h = 18;

    /* Clear the list area in safe strips (320*18=5760 per strip, fits in buffer[40000]) */
    for (int s = 0; s < BROWSER_LIMIT; s++) {
      draw_mask(0, y_start + s * row_h, 320, row_h);
    }
    /* Clear any remaining pixels below last row */
    draw_mask(0, y_start + BROWSER_LIMIT * row_h, 320, 4);

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
      draw_text(x + 20, y + 3, FILES[n], true, selected, false);

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
      int bar_x = 314;
      int bar_h = BROWSER_LIMIT * row_h;
      int thumb_h = (BROWSER_LIMIT * bar_h) / ROMS.total;
      if (thumb_h < 4) thumb_h = 4;
      int thumb_y = y_start + (ROMS.offset * (bar_h - thumb_h)) / (ROMS.total - 1);

      /* Draw track */
      for (int i = 0; i < bar_h; i++) buffer[i] = GUI.bg;
      /* darken track slightly — use fg at low intensity */
      draw_mask(bar_x, y_start, 4, bar_h);

      /* Draw thumb */
      for (int i = 0; i < thumb_h * 4; i++) buffer[i] = GUI.fg;
      ili9341_write_frame_rectangleLE(bar_x, thumb_y, 4, thumb_h, buffer);
    }
  }

  /*
   * draw_browser_row - Redraw a single row in the browser list.
   * n = row index within visible page (0..BROWSER_LIMIT-1)
   */
  void draw_browser_row(int n) {
    int x = 8;
    int y_start = 20;
    int row_h = 18;

    int limit = (ROMS.total - ROMS.offset) < BROWSER_LIMIT ?
      (ROMS.total - ROMS.offset) : BROWSER_LIMIT;
    if (n < 0 || n >= limit) return;

    int y = y_start + n * row_h;
    bool selected = (n == BROWSER_SEL);

    /* Clear this row */
    draw_mask(0, y, 320, row_h);

    /* Draw icon */
    bool is_dir = strcmp(&FILES[n][strlen(FILES[n]) - 3], "dir") == 0;
    if (is_dir) {
      draw_folder(x, y, selected);
    } else {
      draw_media(x, y, selected, -1);
    }

    /* Draw filename */
    draw_text(x + 20, y + 3, FILES[n], true, selected, false);

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
    int y_start = 20;
    int row_h = 18;
    if (ROMS.total > BROWSER_LIMIT) {
      int bar_x = 314;
      int bar_h = BROWSER_LIMIT * row_h;
      int thumb_h = (BROWSER_LIMIT * bar_h) / ROMS.total;
      if (thumb_h < 4) thumb_h = 4;
      int thumb_y = y_start + (ROMS.offset * (bar_h - thumb_h)) / (ROMS.total - 1);
      draw_mask(bar_x, y_start, 4, bar_h);
      for (int i = 0; i < thumb_h * 4; i++) buffer[i] = GUI.fg;
      ili9341_write_frame_rectangleLE(bar_x, thumb_y, 4, thumb_h, buffer);
    }
  }

  /*
   * browser_partial_update - Redraw only the old/new rows + header
   * oldSel = previous BROWSER_SEL, newSel = new BROWSER_SEL
   */
  void browser_partial_update(int oldSel, int newSel) {
    draw_browser_row(oldSel);
    draw_browser_row(newSel);
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
      int cx = (320 - strlen(msg) * 5) / 2;
      draw_text(cx, 120, msg, false, false, false);
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
    draw_battery();
    draw_speaker();
    draw_contrast();
    display_flush();

  //{#pragma region Gamepad
    while (true) {
      odroid_input_gamepad_read(&gamepad);

      /* ============================================================
       *  CAROUSEL MODE  (system selection)
       * ============================================================ */
      if (!BROWSER && !LAUNCHER) {

        /* --- LEFT: previous system --- */
        if (gamepad.values[ODROID_INPUT_LEFT]) {
          if (!SETTINGS && (STEP != 0 || SETTING == 0)) {
            STEP--;
            if (STEP < 0) STEP = COUNT - 1;
            ROMS.offset = 0;
            animate(-1);
          } else if (STEP == 0 && !SETTINGS) {
            /* Settings adjustments: LEFT decreases */
            if (SETTING == 1) {
              nvs_handle handle;
              nvs_open("storage", NVS_READWRITE, &handle);
              nvs_set_i8(handle, "COLOR", 0);
              nvs_commit(handle);
              nvs_close(handle);
              draw_toggle();
              draw_systems();
              display_flush();
            }
            if (SETTING == 2 && VOLUME > 0) { VOLUME--; set_volume(); usleep(200000); }
            if (SETTING == 3 && BRIGHTNESS > 0) { BRIGHTNESS--; set_brightness(); usleep(200000); }
            if (SETTING == 4) {
              nvs_handle handle;
              nvs_open("storage", NVS_READWRITE, &handle);
              nvs_set_i8(handle, "COVER", 0);
              nvs_commit(handle);
              nvs_close(handle);
              draw_cover_toggle();
              display_flush();
            }
          }
          usleep(100000);
        }

        /* --- RIGHT: next system --- */
        if (gamepad.values[ODROID_INPUT_RIGHT]) {
          if (!SETTINGS && (STEP != 0 || SETTING == 0)) {
            STEP++;
            if (STEP > COUNT - 1) STEP = 0;
            ROMS.offset = 0;
            animate(1);
          } else if (STEP == 0 && !SETTINGS) {
            if (SETTING == 1) {
              nvs_handle handle;
              nvs_open("storage", NVS_READWRITE, &handle);
              nvs_set_i8(handle, "COLOR", 1);
              nvs_commit(handle);
              nvs_close(handle);
              draw_toggle();
              draw_systems();
              display_flush();
            }
            if (SETTING == 2 && VOLUME < 8) { VOLUME++; set_volume(); usleep(200000); }
            if (SETTING == 3 && BRIGHTNESS < (BRIGHTNESS_COUNT-1)) { BRIGHTNESS++; set_brightness(); usleep(200000); }
            if (SETTING == 4) {
              nvs_handle handle;
              nvs_open("storage", NVS_READWRITE, &handle);
              nvs_set_i8(handle, "COVER", 1);
              nvs_commit(handle);
              nvs_close(handle);
              draw_cover_toggle();
              display_flush();
            }
          }
          usleep(100000);
        }

        /* --- UP/DOWN: settings navigation when on STEP 0 --- */
        if (gamepad.values[ODROID_INPUT_UP]) {
          if (STEP == 0) {
            if (!SETTINGS) {
              SETTING--;
              if (SETTING < 0) SETTING = 5;
              draw_settings();
            } else {
              USER--;
              if (USER < 0) USER = 21;
              draw_themes();
            }
          }
          usleep(200000);
        }

        if (gamepad.values[ODROID_INPUT_DOWN]) {
          if (STEP == 0) {
            if (!SETTINGS) {
              SETTING++;
              if (SETTING > 5) SETTING = 0;
              draw_settings();
            } else {
              USER++;
              if (USER > 21) USER = 0;
              draw_themes();
            }
          }
          usleep(200000);
        }

        /* --- A: enter ROM browser / settings action --- */
        if (gamepad.values[ODROID_INPUT_A]) {
          if (STEP == 0) {
            /* Settings logic (unchanged) */
            if (!SETTINGS && SETTING == 0) {
              SETTINGS = true;
              draw_background();
              draw_systems();
              draw_text(16, 16, (char *)"THEMES", false, true, false);
              draw_themes();
            } else {
              switch (SETTING) {
                case 0: update_theme(); break;
                case 1: set_toggle(); draw_toggle(); draw_systems(); break;
                case 4: set_cover_toggle(); draw_cover_toggle(); break;
                case 5: delete_recents(); break;
              }
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

        /* --- B: exit settings if open --- */
        if (gamepad.values[ODROID_INPUT_B]) {
          if (SETTINGS) {
            SETTINGS = false;
            draw_background();
            draw_systems();
            draw_text(16, 16, EMULATORS[STEP], false, true, false);
            draw_settings();
          }
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
              /* Cursor moves up within the visible page — partial redraw */
              int oldSel = BROWSER_SEL;
              BROWSER_SEL--;
              if (STEP >= 3) {
                browser_partial_update(oldSel, BROWSER_SEL);
              } else {
                char **list = STEP == 1 ? FAVORITES : RECENTS;
                favrecent_partial_update(oldSel, BROWSER_SEL, list);
              }
            } else if (ROMS.offset > 0) {
              /* At top of page, scroll up by one — highlight stays at top */
              ROMS.offset--;
              if (STEP >= 3) {
                browser_seek_and_draw();
              } else {
                STEP == 1 ? process_favorites() : process_recents();
              }
            } else {
              /* At very first item — wrap to last */
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
              /* Cursor moves down within the visible page — partial redraw */
              int oldSel = BROWSER_SEL;
              BROWSER_SEL++;
              if (STEP >= 3) {
                browser_partial_update(oldSel, BROWSER_SEL);
              } else {
                char **list = STEP == 1 ? FAVORITES : RECENTS;
                favrecent_partial_update(oldSel, BROWSER_SEL, list);
              }
            } else if (ROMS.offset + BROWSER_LIMIT < ROMS.total) {
              /* At bottom of page, scroll down by one — highlight stays at bottom */
              ROMS.offset++;
              if (STEP >= 3) {
                browser_seek_and_draw();
              } else {
                STEP == 1 ? process_favorites() : process_recents();
              }
            } else {
              /* At very last item — wrap to first */
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
                int cx = (320 - strlen(msg) * 5) / 2;
                draw_text(cx, 120, msg, false, false, false);
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
              if (!SAVED) draw_launcher_options();
              break;
            case 2:
              rom_delete_save();
              break;
            case 3:
              ROM.favorite ? delete_favorite(path) : add_favorite(path);
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

      /* --- MENU: restart --- */
      if (gamepad.values[ODROID_INPUT_MENU]) {
        usleep(10000);
        esp_restart();
        debounce(ODROID_INPUT_MENU);
      }

      /* Yield to RTOS so watchdog & other tasks can run */
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  //}#pragma endregion GamePad
  }
//}#pragma endregion Launcher
