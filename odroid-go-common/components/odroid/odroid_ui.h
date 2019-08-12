#pragma once

#include <stdint.h>
#include <string.h>
#include "esp_system.h"
#include "odroid_input.h"
#include "colors_rgb565.h"

extern bool config_speedup;

#ifndef ODROID_UI_CALLCONV
#  if defined(__GNUC__) && defined(__i386__) && !defined(__x86_64__)
#    define ODROID_UI_CALLCONV __attribute__((cdecl))
#  elif defined(_MSC_VER) && defined(_M_X86) && !defined(_M_X64)
#    define ODROID_UI_CALLCONV __cdecl
#  else
#    define ODROID_UI_CALLCONV /* all other platforms only have one calling convention each */
#  endif
#endif

#ifndef RETRO_API
#  if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
#    ifdef RETRO_IMPORT_SYMBOLS
#      ifdef __GNUC__
#        define RETRO_API ODROID_UI_CALLCONV __attribute__((__dllimport__))
#      else
#        define RETRO_API ODROID_UI_CALLCONV __declspec(dllimport)
#      endif
#    else
#      ifdef __GNUC__
#        define RETRO_API ODROID_UI_CALLCONV __attribute__((__dllexport__))
#      else
#        define RETRO_API ODROID_UI_CALLCONV __declspec(dllexport)
#      endif
#    endif
#  else
#      if defined(__GNUC__) && __GNUC__ >= 4 && !defined(__CELLOS_LV2__)
#        define RETRO_API ODROID_UI_CALLCONV __attribute__((__visibility__("default")))
#      else
#        define RETRO_API ODROID_UI_CALLCONV
#      endif
#  endif
#endif

typedef enum
{
    ODROID_UI_FUNC_TOGGLE_RC_NOTHING = 0,
    ODROID_UI_FUNC_TOGGLE_RC_CHANGED = 1,
    ODROID_UI_FUNC_TOGGLE_RC_MENU_RESTART = 2,
    ODROID_UI_FUNC_TOGGLE_RC_MENU_CLOSE = 3,
} odroid_ui_func_toggle_rc;

typedef struct odroid_ui_entry odroid_ui_entry;
typedef struct odroid_ui_window odroid_ui_window;
typedef void (ODROID_UI_CALLCONV *odroid_ui_func_update_def)(odroid_ui_entry *entry);
typedef odroid_ui_func_toggle_rc (ODROID_UI_CALLCONV *odroid_ui_func_toggle_def)(odroid_ui_entry *entry, odroid_gamepad_state *joystick);
typedef void (ODROID_UI_CALLCONV *odroid_ui_func_window_init_def)(odroid_ui_window *entry);

#define color_default C_GRAY
#define color_selected C_WHITE
//#define color_bg_default 0x0008 // ORIG
#define color_bg_default C_NAVY
// 0x0014, C_MIDNIGHT_BLUE, C_NAVY, C_DARK_BLUE

typedef struct odroid_ui_entry
{
    uint16_t x;
    uint16_t y;
    char text[64];
    
    char data[64];
    
    odroid_ui_func_update_def func_update;
    odroid_ui_func_toggle_def func_toggle;
} odroid_ui_entry;

typedef struct odroid_ui_window
{
    uint16_t x;
    uint16_t y;
    
    uint8_t width;
    uint8_t height;
    
    uint8_t border;
    
    uint8_t entry_count;
    odroid_ui_entry* entries[16];
} odroid_ui_window;

bool odroid_ui_menu(bool restart_menu);
bool odroid_ui_menu_ext(bool restart_menu, odroid_ui_func_window_init_def func_window_init);
void odroid_ui_debug_enter_loop();
bool odroid_ui_ask(const char *text);
int odroid_ui_ask_v2(const char *text, uint16_t color_fg, uint16_t color_bg, int selected_initial);
bool odroid_ui_error(const char *text);

void QuickSaveSetBuffer(void* data);
extern bool QuickLoadState(FILE *f);
extern bool QuickSaveState(FILE *f);

odroid_ui_entry *odroid_ui_create_entry(odroid_ui_window *window, odroid_ui_func_update_def func_update, odroid_ui_func_toggle_def func_toggle);

void clean_draw_buffer();
void draw_chars(uint16_t x, uint16_t y, uint16_t width, char *text, uint16_t color, uint16_t color_bg);
void wait_for_key(int last_key);
void odroid_ui_battery_draw(int x, int y, int max, int value);
