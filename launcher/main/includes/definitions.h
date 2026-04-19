/*
  Screen Dimensions in Pixels
*/
#ifdef CONFIG_HDMI_OUTPUT
#define WIDTH 640
#else
#define WIDTH 800
#endif
#define HEIGHT 480

/*
  System Ribbon Display
*/
#define GAP 120
#define NEXT 520

/*
  Show Battery Status
*/
#ifdef CONFIG_HDMI_OUTPUT
#define BATTERY false
#else
#define BATTERY true
#endif

/*
  Global Colors
*/
#define WHITE 65535
#define BLACK 0

/*
  Emulator Count + 3 for Settings, Favorites, and Recently Played
*/
#define COUNT 18

/*

*/
#define MAX_FILES 8192
#define MAX_LENGTH 64
#define MAX_FILES_LIST 512


/*

*/
#define RETROESP_FOLDER "RetroESP32"
#define FAVORITE_FILE "favorites.txt"
#define RECENT_FILE "recent.txt"

/*

*/
#define BUILD "Version 2 Build 9 (v.2.9)"


#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
