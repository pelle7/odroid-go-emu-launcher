#pragma GCC optimize ("O3")

#include "odroid_ui.h"
#include "odroid_system.h"
#include "esp_system.h"
#include "esp_event.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/rtc_io.h"
#include "odroid_display.h"
#include "odroid_input.h"
#include "odroid_audio.h"
#include <stdio.h>
#include <string.h>
//#include <stdint.h>
//#include <stddef.h>
//#include <limits.h>
#include "font8x8_basic.h"

extern bool QuickLoadState(FILE *f);
extern bool QuickSaveState(FILE *f);

bool MyQuickLoadState();
bool MyQuickSaveState();

extern bool scaling_enabled;
bool config_speedup;

static bool short_cut_menu_open = false;

uint16_t *odroid_ui_framebuffer = NULL;

// 0x7D000
#define QUICK_SAVE_BUFFER_SIZE (512 * 1024)
static void *quicksave_buffer = NULL;

static bool quicksave_done = false;

extern const char* SD_BASE_PATH;

const char* SD_TMP_PATH_SAVE = "/sd/odroid/data/.quicksav.dat";
    
char buf[42];

int exec_menu(bool *restart_menu, odroid_ui_func_window_init_def func_window_init);

void QuickSaveSetBuffer(void* data) {
   quicksave_buffer = data;
}

void clean_draw_buffer() {
	int size = 320 * 8 * sizeof(uint16_t);
	memset(odroid_ui_framebuffer, 0, size);
}

void prepare() {
	if (odroid_ui_framebuffer) return;
	printf("odroid_ui_menu: SETUP buffer\n");
	int size = 320 * 8 * sizeof(uint16_t);
    odroid_ui_framebuffer = (uint16_t *)malloc(size);
	if (!odroid_ui_framebuffer) abort();
	clean_draw_buffer();
}

void renderToStdout(char *bitmap) {
    int x,y;
    int set;
    for (x=0; x < 8; x++) {
        for (y=0; y < 8; y++) {
            set = bitmap[x] & 1 << y;
            printf("%c", set ? 'X' : ' ');
        }
        printf("\n");
    }
}

inline void renderToFrameBuffer(int xo, int yo, char *bitmap, uint16_t color, uint16_t color_bg, uint16_t width) {
    int x,y;
    int set;
    for (x=0; x < 8; x++) {
        for (y=0; y < 8; y++) {
            // color++;
            set = bitmap[x] & 1 << y;
            // int offset = xo + x + ((yo + y) * width);
            int offset = xo + y + ((yo + x) * width);
            odroid_ui_framebuffer[offset] = set?color:color_bg; 
        }
    }
}

inline void render2(int offset_x, int offset_y, const char *text, uint16_t color, uint16_t color_bg) {
    int len = strlen(text);
    int x, y;
    uint16_t width = len * 8;
    //char pp[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    x = offset_x * 8;
    y = offset_y * 8;
    for (int i = 0; i < len; i++) {
       unsigned char c;
       c = text[i];
       renderToFrameBuffer(x, y, font8x8_basic[c], color, color_bg, width);
       //renderToFrameBuffer(x, y, &font8x8_basic2[((uint16_t)c)*8], color, color_bg, width);
       x+=8;
    }
}

inline void render(int offset_x, int offset_y, uint16_t text_len, const char *text, uint16_t color, uint16_t color_bg) {
	int len = strlen(text);
    int x, y;
    uint16_t width = text_len * 8;
    x = offset_x * 8;
    y = offset_y * 8;
	//for (int i = 0; i < len; i++) {
	for (int i = 0; i < text_len; i++) {
	   unsigned char c;
	   if (i < len) {
	      c = text[i];
	   } else {
	      c = ' ';
	   }
	   renderToFrameBuffer(x, y, font8x8_basic[c], color, color_bg, width);
	   x+=8;
	   //if (x>=width) {
	   //	break;
	   //}
	}
}

void draw_line(const char *text) {
	render(0,0,320/8,text, color_selected, 0);
	ili9341_write_frame_rectangleLE(0, 0, 320, 8, odroid_ui_framebuffer);
}

void draw_char(uint16_t x, uint16_t y, char c, uint16_t color) {
	renderToFrameBuffer(0, 0, font8x8_basic[(unsigned char)c], color, 0, 8);
	ili9341_write_frame_rectangleLE(x, y, 8, 8, odroid_ui_framebuffer);
}

void draw_chars(uint16_t x, uint16_t y, uint16_t width, char *text, uint16_t color, uint16_t color_bg) {
	render(0, 0, width, text, color, color_bg);
	ili9341_write_frame_rectangleLE(x, y, width * 8, 8, odroid_ui_framebuffer);
}

void draw_empty_line() {
	char tmp[8];
	clean_draw_buffer();
	sprintf(tmp, " ");
	draw_line(tmp);
}

void draw_fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color) {
	int count = width * height;
	if (count> 320 * 8) {
		int height_block = (320 * 8) / width;
		uint16_t yy = y;
		uint16_t ymax = y + height;
		
		int i = 0;
		int count = height_block * width;
		while (i < count) {
			odroid_ui_framebuffer[i] = color;
			i++;
		}
		
		while (yy < ymax) {
		   if (yy + height_block > ymax) {
		   	height_block = ymax - yy;
		   }
		   ili9341_write_frame_rectangleLE(x, yy, width, height_block, odroid_ui_framebuffer);
		   yy += height_block;
		}
		return;
	}
	int i = 0;
	while (i < count) {
		odroid_ui_framebuffer[i] = color;
		i++;
	}
	ili9341_write_frame_rectangleLE(x, y, width, height, odroid_ui_framebuffer);
}

void wait_for_key(int last_key) {
	while (true)
    {
        odroid_gamepad_state joystick;
        odroid_input_gamepad_read(&joystick);
        
        if (!joystick.values[last_key]) {
        		break;
        }
        usleep(25*1000UL);
    }
}

bool odroid_ui_menu(bool restart_menu) {
    return odroid_ui_menu_ext(restart_menu, NULL);
}

bool odroid_ui_menu_ext(bool restart_menu, odroid_ui_func_window_init_def func_window_init) {
    int last_key = -1;
	int start_key = ODROID_INPUT_VOLUME;
	bool shortcut_key = false;
	odroid_gamepad_state lastJoysticState;

	usleep(25*1000UL);
	prepare();
	clean_draw_buffer();
	odroid_input_gamepad_read(&lastJoysticState);
	odroid_display_lock();
	odroid_audio_mute();
    
	//draw_empty_line();
    //draw_line("Press...");
    
	while (!restart_menu)
    {
        odroid_gamepad_state joystick;
        odroid_input_gamepad_read(&joystick);
        
        if (!joystick.values[start_key]) {
        		last_key = start_key;
        		break;
        }
        if (last_key >=0) {
        		if (!joystick.values[last_key]) {
        			draw_line("");
        			last_key = -1;
        		}
        } else {
	        if (!lastJoysticState.values[ODROID_INPUT_UP] && joystick.values[ODROID_INPUT_UP]) {
	        		shortcut_key = true;
	        		short_cut_menu_open = true;
	        		// return;
	        		last_key = ODROID_INPUT_UP;
	        		odroid_audio_volume_change();
	        		sprintf(buf, "Volume changed to: %d", odroid_audio_volume_get()); 
	        		draw_line(buf);
	        }
	        if (!lastJoysticState.values[ODROID_INPUT_B] && joystick.values[ODROID_INPUT_B]) {
		        shortcut_key = true;
	        		last_key = ODROID_INPUT_B;
	        		sprintf(buf, "SAVE");
	        		draw_line(buf);
	        		if (MyQuickSaveState()) {
        				sprintf(buf, "SAVE: Ok");
	        			draw_line(buf);
	        			quicksave_done = true;
        			} else {
	        			sprintf(buf, "SAVE: Error");
	        			draw_line(buf);
        			}
	        }
	        if (!lastJoysticState.values[ODROID_INPUT_A] && joystick.values[ODROID_INPUT_A]) {
		        shortcut_key = true;
	        		last_key = ODROID_INPUT_A;
	        		sprintf(buf, "LOAD");
	        		draw_line(buf);
	        		if (!quicksave_done) {
	        			sprintf(buf, "LOAD: no quicksave");
	        			draw_line(buf);
	        		} else if (MyQuickLoadState()) {
        				sprintf(buf, "LOAD: Ok");
	        			draw_line(buf);
	        			quicksave_done = true;
        			} else {
	        			sprintf(buf, "LOAD: Error");
	        			draw_line(buf);
        			}
	        }
	    }
        lastJoysticState = joystick;
    }
    restart_menu = false;
    if (!shortcut_key) {
		last_key = exec_menu(&restart_menu, func_window_init);
	}
    if (shortcut_key) {
    		draw_empty_line();
    }
    wait_for_key(last_key);
    printf("odroid_ui_menu! Continue\n");
    odroid_display_unlock();
    return restart_menu;
}

odroid_ui_entry *odroid_ui_create_entry(odroid_ui_window *window, odroid_ui_func_update_def func_update, odroid_ui_func_toggle_def func_toggle) {
	odroid_ui_entry *rc = (odroid_ui_entry*)malloc(sizeof(odroid_ui_entry));
	rc->func_update = func_update;
	rc->func_toggle = func_toggle;
	sprintf(rc->data, " ");
	window->entries[window->entry_count++] = rc;
	return rc;
}

void odroid_ui_entry_update(odroid_ui_window *window, uint8_t nr, uint16_t color) {
	odroid_ui_entry *entry = window->entries[nr];
	entry->func_update(entry);
	draw_chars(window->x, window->y + nr * 8, window->width, entry->text, color, color_bg_default);
}

#define ADD_1 2
#define ADD_2 4

void odroid_ui_window_clear(odroid_ui_window *window) {
	draw_fill(window->x - 3, window->y - 3, window->width * 8 + 3 * 2, window->height * 8 + 3 * 2, color_black);
}

void odroid_ui_windows_draw_border(int x, int y, int width, int height, uint16_t color_bg)
{
    draw_fill(x + ADD_2, y - 3, width - ADD_2 * 2, 1, color_bg);
    draw_fill(x + ADD_1, y - 2, width - ADD_1 * 2, 1, color_bg);
    draw_fill(x, y - 1, width, 1, color_bg);
    
    draw_fill(x, y + height + 0, width, 1, color_bg);
    draw_fill(x + ADD_1, y + height + 1, width - ADD_1 * 2, 1, color_bg);
    draw_fill(x + ADD_2, y + height + 2, width - ADD_2 * 2, 1, color_bg);
    
    draw_fill(x - 3, y + ADD_2, 1, height - ADD_2 * 2, color_bg);
    draw_fill(x - 2, y + ADD_1, 1, height - ADD_1 * 2, color_bg);
    draw_fill(x - 1, y, 1, height, color_bg);
    
    draw_fill(x + width, y, 1, height, color_bg);
    draw_fill(x + width + 1, y + ADD_1, 1, height - ADD_1 * 2, color_bg);
    draw_fill(x + width + 2, y + ADD_2, 1, height - ADD_2 * 2, color_bg);
}

void odroid_ui_window_update(odroid_ui_window *window) {
	char text[64] = " ";
	
	for (uint16_t y = 0; y < window->height; y++) {
		draw_chars(window->x, window->y + y * 8, window->width, text, color_default, color_bg_default);
	}
	draw_fill(window->x + ADD_2, window->y - 3, window->width * 8 - ADD_2 * 2, 1, color_bg_default);
	draw_fill(window->x + ADD_1, window->y - 2, window->width * 8 - ADD_1 * 2, 1, color_bg_default);
	draw_fill(window->x, window->y - 1, window->width * 8, 1, color_bg_default);
	
	draw_fill(window->x, window->y + window->height * 8 + 0, window->width * 8, 1, color_bg_default);
	draw_fill(window->x + ADD_1, window->y + window->height * 8 + 1, window->width * 8 - ADD_1 * 2, 1, color_bg_default);
	draw_fill(window->x + ADD_2, window->y + window->height * 8 + 2, window->width * 8 - ADD_2 * 2, 1, color_bg_default);
	
	draw_fill(window->x - 3, window->y + ADD_2, 1, window->height * 8 - ADD_2 * 2, color_bg_default);
	draw_fill(window->x - 2, window->y + ADD_1, 1, window->height * 8 - ADD_1 * 2, color_bg_default);
	draw_fill(window->x - 1, window->y, 1, window->height * 8, color_bg_default);
	
	draw_fill(window->x + window->width * 8, window->y, 1, window->height * 8, color_bg_default);
	draw_fill(window->x + window->width * 8 + 1, window->y + ADD_1, 1, window->height * 8 - ADD_1 * 2, color_bg_default);
	draw_fill(window->x + window->width * 8 + 2, window->y + ADD_2, 1, window->height * 8 - ADD_2 * 2, color_bg_default);
	
	
	for (int i = 0;i < window->entry_count; i++) {
		odroid_ui_entry_update(window, i, color_default);
	}
}

void odroid_ui_func_update_speedup(odroid_ui_entry *entry) {
	sprintf(entry->text, "%-9s: %d", "speedup", config_speedup);
}

void odroid_ui_func_update_volume(odroid_ui_entry *entry) {
	sprintf(entry->text, "%-9s: %d", "vol", odroid_audio_volume_get());
}

void odroid_ui_func_update_scale(odroid_ui_entry *entry) {
	sprintf(entry->text, "%-9s: %s", "scale", scaling_enabled?"on":"off");
}

void odroid_ui_func_update_brightness(odroid_ui_entry *entry) {
    if (!is_backlight_initialized()) {
        sprintf(entry->text, "%-9s: %s", "bright", "n/a");
        return;
    }
    sprintf(entry->text, "%-9s: %d", "bright", odroid_settings_Backlight_get());
}

void odroid_ui_func_update_quicksave(odroid_ui_entry *entry) {
	sprintf(entry->text, "%-9s: %s", "quicksave", entry->data);
}

void odroid_ui_func_update_quickload(odroid_ui_entry *entry) {
	if (quicksave_done) {
		sprintf(entry->text, "%-9s: %s", "quickload", entry->data);
	} else { 
		sprintf(entry->text, "%-9s: n/a", "quickload");
	}
}

odroid_ui_func_toggle_rc odroid_ui_func_toggle_speedup(odroid_ui_entry *entry, odroid_gamepad_state *joystick) {
	config_speedup = !config_speedup;
	return ODROID_UI_FUNC_TOGGLE_RC_MENU_CLOSE;
}

odroid_ui_func_toggle_rc odroid_ui_func_toggle_volume(odroid_ui_entry *entry, odroid_gamepad_state *joystick) {
    odroid_volume_level old = odroid_audio_volume_get();
    if (joystick->values[ODROID_INPUT_A] || joystick->values[ODROID_INPUT_RIGHT]) {
        old = (old+1)%ODROID_VOLUME_LEVEL_COUNT;
    } else if (joystick->values[ODROID_INPUT_LEFT]) {
        old = (old-1 + ODROID_VOLUME_LEVEL_COUNT)%ODROID_VOLUME_LEVEL_COUNT;
    }
    odroid_audio_volume_set(old);
    odroid_settings_Volume_set(old);
    return ODROID_UI_FUNC_TOGGLE_RC_CHANGED;
}

odroid_ui_func_toggle_rc odroid_ui_func_toggle_scale(odroid_ui_entry *entry, odroid_gamepad_state *joystick) {
	scaling_enabled = !scaling_enabled;
	return ODROID_UI_FUNC_TOGGLE_RC_MENU_RESTART;
}

odroid_ui_func_toggle_rc odroid_ui_func_toggle_brightness(odroid_ui_entry *entry, odroid_gamepad_state *joystick) {
    if (joystick->values[ODROID_INPUT_A] || joystick->values[ODROID_INPUT_RIGHT]) {
        odroid_Backlight_set(odroid_Backlight_get() + 1);
    } else if (joystick->values[ODROID_INPUT_LEFT]) {
        odroid_Backlight_set(odroid_Backlight_get() - 1);
    }
    return ODROID_UI_FUNC_TOGGLE_RC_CHANGED;
}

odroid_ui_func_toggle_rc odroid_ui_func_toggle_quicksave(odroid_ui_entry *entry, odroid_gamepad_state *joystick) {
    if (!joystick->values[ODROID_INPUT_A]) return ODROID_UI_FUNC_TOGGLE_RC_NOTHING; 
	if (!MyQuickSaveState()) {
		sprintf(entry->data, "ERR");
		return ODROID_UI_FUNC_TOGGLE_RC_NOTHING;
	}
	quicksave_done = true;
	sprintf(entry->data, "OK");
	return ODROID_UI_FUNC_TOGGLE_RC_MENU_CLOSE;
}

odroid_ui_func_toggle_rc odroid_ui_func_toggle_quickload(odroid_ui_entry *entry, odroid_gamepad_state *joystick) {
    if (!joystick->values[ODROID_INPUT_A]) return ODROID_UI_FUNC_TOGGLE_RC_NOTHING;
    if (!quicksave_done) {
    		return ODROID_UI_FUNC_TOGGLE_RC_NOTHING;
    }
	if (!MyQuickLoadState()) {
		sprintf(entry->data, "ERR");
		return ODROID_UI_FUNC_TOGGLE_RC_NOTHING;
	}
	sprintf(entry->data, "OK");
	return ODROID_UI_FUNC_TOGGLE_RC_MENU_CLOSE;
}

int selected = 0;

int exec_menu(bool *restart_menu, odroid_ui_func_window_init_def func_window_init) {
    int selected_last = selected;
	int last_key = -1;
	int counter = 0;
	odroid_ui_window window;
	window.width = 16;
	window.x = 320 - window.width * 8 - 10;
	window.y = 40;
	window.entry_count = 0;
	
	//odroid_ui_create_entry(&window, &odroid_ui_func_update_speedup, &odroid_ui_func_toggle_speedup);
	odroid_ui_create_entry(&window, &odroid_ui_func_update_volume, &odroid_ui_func_toggle_volume);
	odroid_ui_create_entry(&window, &odroid_ui_func_update_scale, &odroid_ui_func_toggle_scale);
	odroid_ui_create_entry(&window, &odroid_ui_func_update_brightness, &odroid_ui_func_toggle_brightness);
	
	if (func_window_init) {
	   func_window_init(&window);
	}
	
	odroid_ui_create_entry(&window, &odroid_ui_func_update_quicksave, &odroid_ui_func_toggle_quicksave);
	odroid_ui_create_entry(&window, &odroid_ui_func_update_quickload, &odroid_ui_func_toggle_quickload);
	
	window.height = window.entry_count;
	odroid_ui_window_update(&window);
	odroid_ui_entry_update(&window, selected, color_selected);
	bool run = true;
	while (!*restart_menu && run)
    {
        odroid_gamepad_state joystick;
        odroid_input_gamepad_read(&joystick);
        odroid_ui_func_toggle_rc entry_rc = ODROID_UI_FUNC_TOGGLE_RC_NOTHING;
        selected_last = selected;
        if (last_key >= 0) {
        		if (!joystick.values[last_key]) {
        			last_key = -1;
        		}
        } else {
	        if (joystick.values[ODROID_INPUT_B]) {
	            last_key = ODROID_INPUT_B;
	        		entry_rc = ODROID_UI_FUNC_TOGGLE_RC_MENU_CLOSE;
	        } else if (joystick.values[ODROID_INPUT_VOLUME]) {
	            last_key = ODROID_INPUT_VOLUME;
	        		entry_rc = ODROID_UI_FUNC_TOGGLE_RC_MENU_CLOSE;
	        } else if (joystick.values[ODROID_INPUT_UP]) {
	        		last_key = ODROID_INPUT_UP;
	        		selected--;
	        		if (selected<0) selected = window.entry_count - 1;
	        } else if (joystick.values[ODROID_INPUT_DOWN]) {
	        		last_key = ODROID_INPUT_DOWN;
	        		selected++;
	        		if (selected>=window.entry_count) selected = 0;
	        } else if (joystick.values[ODROID_INPUT_A]) {
	        		last_key = ODROID_INPUT_A;
	        		entry_rc = window.entries[selected]->func_toggle(window.entries[selected], &joystick);
	        } else if (joystick.values[ODROID_INPUT_LEFT]) {
                last_key = ODROID_INPUT_LEFT;
                entry_rc = window.entries[selected]->func_toggle(window.entries[selected], &joystick);
            } else if (joystick.values[ODROID_INPUT_RIGHT]) {
                last_key = ODROID_INPUT_RIGHT;
                entry_rc = window.entries[selected]->func_toggle(window.entries[selected], &joystick);
            }
        }
        switch (entry_rc) {
        case ODROID_UI_FUNC_TOGGLE_RC_NOTHING:
        		break;
        	case ODROID_UI_FUNC_TOGGLE_RC_CHANGED:
        		odroid_ui_entry_update(&window, selected, color_selected);
        		break;
        	case ODROID_UI_FUNC_TOGGLE_RC_MENU_RESTART:
        	    odroid_ui_entry_update(&window, selected, color_selected);
        		*restart_menu = true;
        		break;
        	case ODROID_UI_FUNC_TOGGLE_RC_MENU_CLOSE:
        	    odroid_ui_entry_update(&window, selected, color_selected);
        		run = false;
        		break;
        	}
        
        if (selected_last != selected) {
        		odroid_ui_entry_update(&window, selected_last, color_default);
        		odroid_ui_entry_update(&window, selected, color_selected);
        }
        
        counter++;
    }
    wait_for_key(last_key);
    odroid_ui_window_clear(&window);
    for (int i = 0; i < window.entry_count; i++) {
        free(window.entries[i]);
    }
   return last_key;
}

void odroid_ui_debug_enter_loop() {
   printf("odroid_ui_debug_enter_loop: go\n");
   prepare();
   config_speedup = false;
}

bool MyQuickSaveState() {
   if (!quicksave_buffer) {
   		// Save to file
   		printf("QuickSave: %s\n", SD_TMP_PATH_SAVE);
   		odroid_system_led_set(1);
	    FILE* f = fopen(SD_TMP_PATH_SAVE, "w");
	    if (f == NULL)
	    {
	        printf("%s: fopen save failed\n", __func__);
	        odroid_system_led_set(0);
	        return false;
	    }
	    bool rc = QuickSaveState(f);
	    fclose(f);
	    printf("%s: savestate OK.\n", __func__);
	    odroid_system_led_set(0);
	    return rc;
   }
   printf("QuickSave: %s\n", "RAM");
   FILE *fileHandleInMemory = fmemopen(quicksave_buffer, QUICK_SAVE_BUFFER_SIZE, "w");
   bool rc = QuickSaveState(fileHandleInMemory);
   fclose(fileHandleInMemory);
   return rc;
}

bool MyQuickLoadState() {
   if (!quicksave_buffer) {
	   FILE* f = fopen(SD_TMP_PATH_SAVE, "r");
	    if (f == NULL)
	    {
	        printf("LoadState: fopen load failed\n");
	        return false;
	    }
        bool rc = QuickLoadState(f);
        printf("LoadState: loadstate OK.\n");
        return rc;
    }
   FILE *fileHandleInMemory = fmemopen(quicksave_buffer, QUICK_SAVE_BUFFER_SIZE, "r");
   bool rc = QuickLoadState(fileHandleInMemory);
   fclose(fileHandleInMemory);
   return rc;
}

bool odroid_ui_popup(const char *text, uint16_t color, uint16_t color_bg)
{
    int len = strlen(text);
    int x = (320 - (len*8))/2;
    int y = 112;
    draw_chars(x, y  , len, " ", color, color_bg);
    draw_chars(x, y+ 8*1, len, text, color, color_bg);
    draw_chars(x, y+ 8*2, len, " ", color, color_bg);
    int last_key = -1;
    bool rc = true;
    while (1)
    {
        odroid_gamepad_state joystick;
        odroid_input_gamepad_read(&joystick);
        
        if (joystick.values[ODROID_INPUT_A]) {
            last_key = ODROID_INPUT_A;
            rc = true;
            break;
        } else if (joystick.values[ODROID_INPUT_B]) {
            last_key = ODROID_INPUT_B;
            rc = false;
            break;
        }
        
        usleep(20*1000UL);
    }
    wait_for_key(last_key);
    return rc;
}

int odroid_ui_ask_v2(const char *text, uint16_t color, uint16_t color_bg, int selected_initial)
{
    int len = strlen(text) + 2;
    int x = (320 - (len*8))/2;
    int y = 112;
    int x2 = (320 - (9*8))/2;
    draw_chars(x, y  , len, " ", color, color_bg);
    draw_chars(x, y+ 8*1, len, " ", color, color_bg);
    draw_chars(x, y+ 8*2, len, " ", color, color_bg);
    draw_chars(x, y+ 8*3, len, " ", color, color_bg);
    odroid_ui_windows_draw_border(x, y, len * 8, 4 * 8, color_bg);
    draw_chars(x + 8, y+ 8*1, len - 2, text, color, color_bg);
    int last_key = -1;
    int rc = selected_initial;
    int rc_old = -1;
    uint16_t color_sel = color_bg;
    uint16_t color_bg_sel = color;
    while (1)
    {
        odroid_gamepad_state joystick;
        odroid_input_gamepad_read(&joystick);
        if (last_key >=0) {
            if (!joystick.values[last_key]) {
                last_key = -1;
            }
        } else {   
            if (joystick.values[ODROID_INPUT_LEFT]) {
                last_key = ODROID_INPUT_LEFT;
                rc = rc==0?1:0;
            } else if (joystick.values[ODROID_INPUT_RIGHT]) {
                last_key = ODROID_INPUT_RIGHT;
                rc = rc==0?1:0;
            } else if (joystick.values[ODROID_INPUT_A]) {
                last_key = ODROID_INPUT_A;
                break;
            } else if (joystick.values[ODROID_INPUT_B]) {
                last_key = ODROID_INPUT_B;
                rc = -1;
                break;
            }
        }
        if (rc_old != rc)
        {
            int nr;
            nr = 0;
            draw_chars(x2, y+ 8*2, 5, " Yes", nr==rc?color_sel:color, nr==rc?color_bg_sel:color_bg);
            nr = 1;
            draw_chars(x2 + 8*5, y+ 8*2, 4, " No", nr==rc?color_sel:color, nr==rc?color_bg_sel:color_bg);
            rc_old = rc;
        }
        
        usleep(20*1000UL);
    }
    wait_for_key(last_key);
    return rc;
}

bool odroid_ui_ask(const char *text)
{
    return odroid_ui_popup(text, color_selected, color_bg_default);
}

bool odroid_ui_error(const char *text)
{
    return odroid_ui_popup(text, color_selected, color_red);
}
