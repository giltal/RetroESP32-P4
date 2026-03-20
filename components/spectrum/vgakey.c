/* 
 * Copyright (C) 1996-1998 Szeredi Miklos
 * Email: mszeredi@inf.bme.hu
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. See the file COPYING. 
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include "config.h"

#include "spkey.h"
#include "spkey_p.h"
#include "spperif.h"
#include "vgascr.h"
#include "akey.h"

#include <stdlib.h>
#include "odroid_input.h"

extern void keyboard_init();
extern void keyboard_close();
extern void keyboard_translatekeys();
extern int keyboard;

extern int menu();
extern void kb_blank();
extern void kb_set();

/* Button-to-key mapping (indices into map[40]):
 * Default: Cursor/Protek joystick layout
 * map[6]=7, map[5]=6, map[4]=5, map[7]=8 → keys 7,6,5,8
 * map[9]=10, map[8]=9 → keys 0, Enter
 * map[39]=57, map[29]=42+? → Space, Shift */
int b_up=6,b_down=5,b_left=4,b_right=7;
int b_a=9,b_b=8,b_select=39,b_start=29;
int pressed=0;  // used for de-bouncing buttons
int kbpos=0; // virtual keyboard position

const int need_switch_mode = 1;
volatile int spvk_after_switch = 0;

/* Virtual keyboard injection (set by spectrum_run.c) */
volatile int zx_vkb_active = 0;
volatile int zx_vkb_inject = -1;   /* scancode to inject, -1 = none */
volatile int zx_vkb_cs = 0;        /* Caps Shift sticky */
volatile int zx_vkb_ss = 0;        /* Symbol Shift sticky */


const int map[40]={
2,3,4,5,6,7,8,9,10,11,
16,17,18,19,20,21,22,23,24,25,
30,31,32,33,34,35,36,37,38,28,
42,44,45,46,47,48,49,50,54,57
};


#define LASTKEYCODE 111

#define KC_F1 59

#define KC_L_SHIFT 42
#define KC_R_SHIFT 54

#define KC_L_CTRL 29
#define KC_R_CTRL 97

#define KC_L_ALT 56
#define KC_R_ALT 100

static char kbstate[LASTKEYCODE];


extern void DoMenuHome(bool save);
#include "z80.h"

extern int my_lastborder;
extern int sp_nosync;
/* In-game menu request flag — set by vgakey, handled by spectrum_run.c */
volatile int sp_menu_request = 0;

/* Kempston joystick bits on port 0x1F */
#define KEMP_RIGHT 0x01
#define KEMP_LEFT  0x02
#define KEMP_DOWN  0x04
#define KEMP_UP    0x08
#define KEMP_FIRE  0x10

void keyboard_update()
{
  odroid_gamepad_state joystick;
  int i;
  
  for (i=0; i<LASTKEYCODE; i++) kbstate[i]=0;  
  odroid_input_gamepad_read(&joystick); 

  /* When virtual keyboard is active, suppress normal gamepad→key mapping */
  if (!zx_vkb_active) {
    /* Cursor/Protek keyboard joystick (keys 5,6,7,8,0) */
    if (joystick.values[ODROID_INPUT_UP])     kbstate[map[b_up]]=1;
    if (joystick.values[ODROID_INPUT_DOWN])   kbstate[map[b_down]]=1; 
    if (joystick.values[ODROID_INPUT_LEFT])   kbstate[map[b_left]]=1;
    if (joystick.values[ODROID_INPUT_RIGHT])  kbstate[map[b_right]]=1; 
    if (joystick.values[ODROID_INPUT_A])      kbstate[map[b_a]]=1; 
    if (joystick.values[ODROID_INPUT_B])      kbstate[map[b_b]]=1; 
    if (joystick.values[ODROID_INPUT_SELECT]) kbstate[map[b_select]]=1; 
    if (joystick.values[ODROID_INPUT_START])  kbstate[map[b_start]]=1;

    /* Kempston joystick — drive port 0x1F directly so games using
       Kempston interface work without extra configuration */
    {
      unsigned char kemp = 0;
      if (joystick.values[ODROID_INPUT_RIGHT]) kemp |= KEMP_RIGHT;
      if (joystick.values[ODROID_INPUT_LEFT])  kemp |= KEMP_LEFT;
      if (joystick.values[ODROID_INPUT_DOWN])  kemp |= KEMP_DOWN;
      if (joystick.values[ODROID_INPUT_UP])    kemp |= KEMP_UP;
      if (joystick.values[ODROID_INPUT_A])     kemp |= KEMP_FIRE;
      if (joystick.values[ODROID_INPUT_B])     kemp |= KEMP_FIRE;
      z80_inports[0x1F] = kemp;
    }
  }

  /* Inject virtual keyboard key presses */
  if (zx_vkb_cs) kbstate[42] = 1;  /* Caps Shift */
  if (zx_vkb_ss) kbstate[54] = 1;  /* Symbol Shift */
  if (zx_vkb_inject >= 0 && zx_vkb_inject < LASTKEYCODE)
    kbstate[zx_vkb_inject] = 1;

  /* X button = open in-game menu */
  if (joystick.values[ODROID_INPUT_MENU]) {
    sp_menu_request = 1;
  }

  if (pressed &&
       !joystick.values[ODROID_INPUT_UP] &&
       !joystick.values[ODROID_INPUT_DOWN] &&
       !joystick.values[ODROID_INPUT_LEFT] &&
       !joystick.values[ODROID_INPUT_RIGHT]
       ) pressed=0;
  
}


static void spkb_init(void)
{
  keyboard_init();
  keyboard_translatekeys();
  // kbstate = keyboard_getstate(); // bjs not needed now
}

#define spkb_close()   keyboard_close()
#define spkb_raw()     keyboard_init()
#define spkb_normal()  keyboard_close()
#define spkb_update()  keyboard_update()

#define kbst(i) kbstate[i]

#define SDEMPTY {0, 0}
#define SDSPEC(x) {x, x}

struct spscan_keydef {
  unsigned norm;
  unsigned shifted;
};

static struct spscan_keydef sp_cp_keycode[] = {
  SDEMPTY,
  SDSPEC(SK_Escape),
  {'1', '!'},
  {'2', '@'},
  {'3', '#'},
  {'4', '$'},
  {'5', '%'},
  {'6', '^'},
  {'7', '&'},
  {'8', '*'},
  {'9', '('},
  {'0', ')'},
  {'-', '_'},
  {'=', '+'},
  SDSPEC(SK_BackSpace),
   
  SDSPEC(SK_Tab),
  {'q', 'Q'},
  {'w', 'W'},
  {'e', 'E'},
  {'r', 'R'},
  {'t', 'T'},
  {'y', 'Y'},
  {'u', 'U'},
  {'i', 'I'},
  {'o', 'O'},
  {'p', 'P'},
  {'[', '{'},
  {']', '}'},
  SDSPEC(SK_Return),
   
  SDSPEC(SK_Control_L),
  {'a', 'A'},
  {'s', 'S'},
  {'d', 'D'},
  {'f', 'F'},
  {'g', 'G'},
  {'h', 'H'},
  {'j', 'J'},
  {'k', 'K'},
  {'l', 'L'},
  {';', ':'},
  {'\'', '"'}, 
  {'`', '~'},
   
  SDSPEC(SK_Shift_L),
  {'\\', '|'},
  {'z', 'Z'},
  {'x', 'X'},
  {'c', 'C'},
  {'v', 'V'},
  {'b', 'B'},
  {'n', 'N'},
  {'m', 'M'},
  {',', '<'},
  {'.', '>'},
  {'/', '?'},
  SDSPEC(SK_Shift_R),
  SDSPEC(SK_KP_Multiply),
  SDSPEC(SK_Alt_L),
   
  {' ', ' '},
  SDSPEC(SK_Caps_Lock),
  SDSPEC(SK_F1),
  SDSPEC(SK_F2),
  SDSPEC(SK_F3),
  SDSPEC(SK_F4),
  SDSPEC(SK_F5),
  SDSPEC(SK_F6),
  SDSPEC(SK_F7),
  SDSPEC(SK_F8),
  SDSPEC(SK_F9),
  SDSPEC(SK_F10),

  SDSPEC(SK_Num_Lock),
  SDSPEC(SK_Scroll_Lock),
  SDSPEC(SK_KP_Home),
  SDSPEC(SK_KP_Up),
  SDSPEC(SK_KP_Page_Up),
  SDSPEC(SK_KP_Subtract),
  SDSPEC(SK_KP_Left),
  SDSPEC(SK_KP_5),
  SDSPEC(SK_KP_Right),
  SDSPEC(SK_KP_Add),
  SDSPEC(SK_KP_End),
  SDSPEC(SK_KP_Down),
  SDSPEC(SK_KP_Page_Down),
  SDSPEC(SK_KP_Insert),
  SDSPEC(SK_KP_Delete),

  SDEMPTY,
  SDEMPTY,
  {'<', '>'},
  SDSPEC(SK_F11),
  SDSPEC(SK_F12),
  SDEMPTY,
  SDEMPTY,
  SDEMPTY,
  SDEMPTY,
  SDEMPTY,
  SDEMPTY,
  SDEMPTY,
  SDSPEC(SK_KP_Enter),
  SDSPEC(SK_Control_R),
  SDSPEC(SK_KP_Divide),
  SDSPEC(SK_Print),
  SDSPEC(SK_Alt_R),
  SDEMPTY,
  SDSPEC(SK_Home),
  SDSPEC(SK_Up),
  SDSPEC(SK_Page_Up),
  SDSPEC(SK_Left),
  SDSPEC(SK_Right),
  SDSPEC(SK_End),
  SDSPEC(SK_Down),
  SDSPEC(SK_Page_Down),
  SDSPEC(SK_Insert),
  SDSPEC(SK_Delete),
};

spkeyboard kb_mkey;

void spkey_textmode(void)
{
  spkb_normal();
  restore_sptextmode();
}

void spkey_screenmode(void)
{
  set_vga_spmode();
  spkb_raw();
  
  sp_init_screen_mark();
}

//------------------------------------------------
void spkb_process_events(int evenframe)
{
  int i;
  int changed;
  
  if(evenframe) {
    spkb_update();
    changed = 0;
    for(i = 0; i <= LASTKEYCODE; i++) {
      int ki;
      int sh;
      int ks;
      
      ks = sp_cp_keycode[i].norm;
      ki = KS_TO_KEY(ks);
    
      if( kbst(i) && !spkb_kbstate[ki].press  ) { // press
	changed = 1;
	spkb_kbstate[ki].press = 1;
	
	sh = sp_cp_keycode[i].shifted;

	spkb_last.modif = 0;

	if(kbst(KC_L_SHIFT) || kbst(KC_R_SHIFT)) 
	  spkb_last.modif |= SKShiftMask;

	if(kbst(KC_L_CTRL) || kbst(KC_R_CTRL)) 
	  spkb_last.modif |= SKControlMask;

	if(kbst(KC_L_ALT) || kbst(KC_R_ALT)) 
	  spkb_last.modif |= SKMod1Mask;
	
	spkb_last.index = ki;
	spkb_last.shifted = sh;
	
	spkb_last.keysym = (spkb_last.modif &  SKShiftMask) ? sh : ks;
	if((!ISFKEY(ks) || !spvk_after_switch) && accept_keys &&
	   !spkey_keyfuncs()) {
	  spkb_kbstate[ki].state = 1;
	  spkb_kbstate[i].frame = sp_int_ctr;
	}
	else spkb_kbstate[ki].state = 0;
	
	spvk_after_switch = 0;
      }
      
      if( !kbst(i) && spkb_kbstate[ki].press ) {  // release
	changed = 1;
	spkb_kbstate[ki].press = 0;
	spkb_kbstate[ki].state = 0;
      }
      
    }
    if(changed) spkb_state_changed = 1;
  }
  process_keys();
}

//---------------------------------------------------

void init_spect_key(void)
{
  int i;
 
  for(i = 0; i < NR_SPKEYS; i++) spkb_kbstate[i].press = 0;
  clear_keystates();
  init_basekeys();
  spkb_init();
  // atexit(close_spect_key); // bjs unnecessary!
}

int display_keyboard(void)
{
  return 0;
}
