/*
 * SDL_event.h compat header for Duke3D PSRAM app.
 * Contains event structures matching original SDL types.
 * Implementation in papp_sdl_event.c.
 */
#ifndef _SDL_events_h
#define _SDL_events_h

#include "SDL_stdinc.h"
#include "SDL_scancode.h"
#include "SDL_input.h"

#define SDL_RELEASED 0
#define SDL_PRESSED  1

typedef enum {
    SDL_FIRSTEVENT = 0,
    SDL_QUIT       = 0x100,
    SDL_WINDOWEVENT = 0x200,
    SDL_SYSWMEVENT,
    SDL_KEYDOWN    = 0x300,
    SDL_KEYUP,
    SDL_TEXTEDITING,
    SDL_TEXTINPUT,
    SDL_MOUSEMOTION    = 0x400,
    SDL_MOUSEBUTTONDOWN,
    SDL_MOUSEBUTTONUP,
    SDL_MOUSEWHEEL,
    SDL_JOYAXISMOTION  = 0x600,
    SDL_JOYBALLMOTION,
    SDL_JOYHATMOTION,
    SDL_JOYBUTTONDOWN,
    SDL_JOYBUTTONUP,
    SDL_JOYDEVICEADDED,
    SDL_JOYDEVICEREMOVED,
    SDL_CONTROLLERAXISMOTION  = 0x650,
    SDL_CONTROLLERBUTTONDOWN,
    SDL_CONTROLLERBUTTONUP,
    SDL_USEREVENT  = 0x8000,
    SDL_LASTEVENT  = 0xFFFF
} SDL_EventType;

typedef struct { Uint32 type; Uint32 timestamp; } SDL_GenericEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8 state;
    Uint8 repeat;
    Uint8 padding2;
    Uint8 padding3;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint8 event;
    Uint8 padding1, padding2, padding3;
    Sint32 data1, data2;
} SDL_WindowEvent;

typedef struct { Uint32 type; Uint32 timestamp; } SDL_QuitEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Sint32 code;
    void *data1;
    void *data2;
} SDL_UserEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Uint8 state;
    Uint8 padding1, padding2, padding3;
    Sint32 x, y, xrel, yrel;
} SDL_MouseMotionEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Uint8 button, state, padding1, padding2;
    Sint32 x, y;
} SDL_MouseButtonEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint32 windowID;
    Uint32 which;
    Sint32 x, y;
} SDL_MouseWheelEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint8 which;
    Uint8 axis;
    Uint8 padding1, padding2, padding3;
    Sint16 value;
    Uint16 padding4;
} SDL_JoyAxisEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint8 which;
    Uint8 hat;
    Uint8 value;
    Uint8 padding1, padding2;
} SDL_JoyHatEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint8 which;
    Uint8 button;
    Uint8 state;
    Uint8 padding1, padding2;
} SDL_JoyButtonEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Uint8 which;
    Uint8 ball;
    Uint8 padding1, padding2;
    Sint16 xrel, yrel;
} SDL_JoyBallEvent;

typedef struct {
    Uint32 type;
    Uint32 timestamp;
    Sint32 which;
} SDL_JoyDeviceEvent;

typedef union SDL_Event {
    Uint32 type;
    SDL_GenericEvent generic;
    SDL_WindowEvent window;
    SDL_KeyboardEvent key;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_JoyAxisEvent jaxis;
    SDL_JoyHatEvent jhat;
    SDL_JoyBallEvent jball;
    SDL_JoyButtonEvent jbutton;
    SDL_JoyDeviceEvent jdevice;
    SDL_QuitEvent quit;
    SDL_UserEvent user;
    Uint8 padding[56];
} SDL_Event;

void SDL_PumpEvents(void);
int SDL_PollEvent(SDL_Event *event);
int SDL_PushEvent(SDL_Event *event);

/* SDL_QUERY / SDL_ENABLE / SDL_DISABLE for event state */
#define SDL_QUERY   -1
#define SDL_ENABLE   1
#define SDL_DISABLE  0

static inline int SDL_JoystickEventState(int state) { return 0; }

/* input init (no-op in papp) */
void inputInit(void);

/* keyMode — used by menues.c to switch button bindings in-menu.
 * Defined in papp_sdl_event.c */
extern int keyMode;

#endif /* _SDL_events_h */
