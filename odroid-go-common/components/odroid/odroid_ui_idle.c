#pragma GCC optimize ("O3")

#include "odroid_ui_idle.h"
#include "odroid_display.h"
#include <unistd.h>
#include "odroid_ui_idle_credits.h"
#include "odroid_settings.h"
#include <dirent.h>
#include "freertos/FreeRTOS.h"

#define ODROID_UI_IDLE_MODE_KEY "IdleMode"

enum OdroidUIIdleMode {
    ODROID_UI_IDLE_MODE_CREDITS = 0,
    ODROID_UI_IDLE_MODE_BLACK = 1,
    ODROID_UI_IDLE_MODE_COVERS = 2,
    ODROID_UI_IDLE_MODE_MAX = 3,
    ODROID_UI_IDLE_MODE_C64 = 10,
};

static int odroid_ui_idle_mode = -1;

static uint16_t colors[] = {
C_YELLOW,
C_GREEN,
C_RED,
C_ORANGE,
C_CYAN,
C_NAVY,
C_BLUE,
C_VIOLET,
C_MAGENTA,
C_PINK,
C_BLACK,
C_SILVER,
};


#define IDLE_CHECK_ABORT \
    odroid_input_gamepad_read(&joystick); \
    if (joystick.values[ODROID_INPUT_A]) { last_key = ODROID_INPUT_A; break; }


uint16_t display_palette16[16];

// C64 color palette (more realistic looking colors)
const uint8_t palette_red[16] = {
    0x00, 0xff, 0x99, 0x00, 0xcc, 0x44, 0x11, 0xff, 0xaa, 0x66, 0xff, 0x40, 0x80, 0x66, 0x77, 0xc0
};

const uint8_t palette_green[16] = {
    0x00, 0xff, 0x00, 0xff, 0x00, 0xcc, 0x00, 0xff, 0x55, 0x33, 0x66, 0x40, 0x80, 0xff, 0x77, 0xc0
};

const uint8_t palette_blue[16] = {
    0x00, 0xff, 0x00, 0xcc, 0xcc, 0x44, 0x99, 0x00, 0x00, 0x00, 0x66, 0x40, 0x80, 0x66, 0xff, 0xc0
};

void c64_init_palette()
{
    for(int i = 0; i < 16; ++i)
    {
        uint8_t r = palette_red[i];
        uint8_t g = palette_green[i];
        uint8_t b = palette_blue[i];

        //rrrr rggg gggb bbbb
        uint16_t rgb565 = ((r << 8) & 0xf800) | ((g << 3) & 0x07e0) | (b >> 3);
        display_palette16[i] = rgb565;
    }
}

void odroid_ui_idle_run_c64_loading()
{
    c64_init_palette();
    int last_key;
    odroid_gamepad_state joystick;
    const char* text = "LOAD \"8\",1";
    int counter = 0;
    int border_width = 32;
    int border_height = 32;
    int width = (320-border_width*2)/8;
    int height = (240-border_height*2)/8;
    
    uint16_t col_f = display_palette16[14];
    uint16_t col_b = display_palette16[6];
    
   for (int i=0;i<height;i++)
   {
    odroid_ui_draw_chars(border_width, border_height+i*8, width, "", col_f, col_b);
   }
    
    while (true)
    {
        IDLE_CHECK_ABORT
        
        counter++;
        for (int y = 0;y<240;y++)
        {
            uint16_t col;
            if (counter<100)
            {
            odroid_ui_draw_chars(border_width, border_height, counter/10+1, text, col_f, col_b);
            col = col_f;
            }
            else
            {
            col = display_palette16[(rand())%16];
            }
            
            for (int a = 0; a< 320; a++)
            {
                odroid_ui_framebuffer[a] = col;
            }
            if (y<border_height || y>=240-border_height)
            {
                ili9341_write_frame_rectangleLE(0, y, 320, 1, odroid_ui_framebuffer);
            }
            else
            {
                ili9341_write_frame_rectangleLE(0, y, border_width, 1, odroid_ui_framebuffer);
                ili9341_write_frame_rectangleLE(320-border_width, y, border_width, 1, odroid_ui_framebuffer);
            }
        } 
        
        usleep(20*1000UL);
    }
    odroid_ui_wait_for_key(last_key, false);
}

void odroid_ui_idle_run_black_once()
{
    odroid_ui_clean_draw_buffer();
    int y = 240;
    while (true)
    {
        if (y-->0)
        {
            ili9341_write_frame_rectangleLE(0, y, 320, 1, odroid_ui_framebuffer);
        } else break;
        
        usleep(1*1000UL);
    }
}

void odroid_ui_idle_run_black()
{
    int last_key;
    odroid_gamepad_state joystick;
    //ili9341_clear(C_BLACK);
    int y = 240;
    while (true)
    {
        IDLE_CHECK_ABORT
        
        if (y-->0)
        {
            ili9341_write_frame_rectangleLE(0, y, 320, 1, odroid_ui_framebuffer);
        } else y = 0;
        
        usleep(20*1000UL);
    }
    odroid_ui_wait_for_key(last_key, false);
}

void odroid_ui_idle_run_credits()
{
    c64_init_palette();
    int last_key;
    odroid_gamepad_state joystick;
    int counter = 0;
    int border_width = 16;
    int border_height = 16;
    int width = (320-border_width*2)/8;
    int height = (240-border_height*2)/8;
    int yy = 0;
    int sleep_time;
    char buf[40];
    //uint16_t col_f = C_WHITE;
    //uint16_t col_b = C_BLACK;
    uint16_t col_f = display_palette16[14];
    uint16_t col_b = display_palette16[6];
    for (int i=0;i<height;i++)
   {
    odroid_ui_draw_chars(border_width, border_height+i*8, width, "", col_f, col_b);
   }
    
    while (true)
    {
        sleep_time = 20*1000UL;
        IDLE_CHECK_ABORT
        bool fast = false;
        {
            int pl = 1;
            if (joystick.values[ODROID_INPUT_UP]) { fast = true; pl = 8; }
            if (joystick.values[ODROID_INPUT_DOWN]) { fast = true; pl = -8; }
            yy+=pl;
            if (yy<0) yy = ODROID_UI_IDLE_CREDITS_COUNT * 8 + (240-32) - 8;
            
            if (fast)
                sleep_time = 1*1000UL;
            
            int min = 0;
            int max = yy/8;
            int y = 240-16 - yy%8 - max * 8 -1;
            if (max - min>26)
            {
                min = max - 26;
                y += min*8; 
            }
            if (max>=ODROID_UI_IDLE_CREDITS_COUNT) max = ODROID_UI_IDLE_CREDITS_COUNT - 1;
            if (min > max) yy = 0;
            
            //printf("%03d: %02d-%02d\n", y, min, max);
            
            for (int i = min;i <=max;i++) {
                  memset(buf, ' ', 39);
                  buf[39] = '\0';
                  const char * text = odroid_ui_idle_credits[i];
                  int len = strlen(text);
                  int offset = (36-len)/2;
                  memcpy(&buf[offset], text, len);
                  int y_from = 0;
                  int y_to = 8;
                  if (y<16) y_from = 16 - y;
                  if (y>240-24) y_to = 8-(y-(240-24));
                  odroid_ui_draw_chars_ext(16, y, 40-4, buf, col_f, col_b, y_from, y_to);
                  y+=8;
            }
        }
        counter++;
        for (int y = 0;y<240;y++)
        {
            uint16_t col;
            col = (rand())%65536;
            
            for (int a = 0; a< 320; a++)
            {
                odroid_ui_framebuffer[a] = col;
            }
            if (y<border_height || y>=240-border_height)
            {
                ili9341_write_frame_rectangleLE(0, y, 320, 1, odroid_ui_framebuffer);
            }
            else
            {
                ili9341_write_frame_rectangleLE(0, y, border_width, 1, odroid_ui_framebuffer);
                ili9341_write_frame_rectangleLE(320-border_width, y, border_width, 1, odroid_ui_framebuffer);
            }
        } 
        
        usleep(sleep_time);
    }
    odroid_ui_wait_for_key(last_key, false);
}

static uint16_t *load_pic(const char *filename, uint16_t *w, uint16_t *h)
{
    uint16_t *rc = NULL;
    FILE *f = fopen(filename, "rb");
    if (f)
    {
        uint16_t width, height;
        fread(&width, 2, 1, f);
        fread(&height, 2, 1, f);
        *w = width;
        *h = height;
        if (width<=320 && height<=176)
        {
            rc = (uint16_t*)heap_caps_malloc(width*height*2, MALLOC_CAP_SPIRAM);
            fread(rc, 2, width*height, f);
        }
        fclose(f);
    }
    return rc;
}

#include "../../../go-emu/main/goemu_util.h"

struct my_folder {
    char *folders_buf;
    uint32_t *folders_ref;
    int count_top;
    int count;
    goemu_util_strings *files;
};

static struct my_folder *read_files()
{
    char buf[128];
    sprintf(buf, "/sd/romart");
    DIR* dir = opendir(buf);
    if (!dir)
    {
        return NULL;
    }
    #define MAX_FOLDERS_COUNT 256
    #define MAX_FOLDERS_SIZE 8192
    char *folders_buf = (char*)heap_caps_malloc(MAX_FOLDERS_SIZE, MALLOC_CAP_SPIRAM);
    uint32_t *folders_ref = (uint32_t *)heap_caps_malloc(MAX_FOLDERS_COUNT*4, MALLOC_CAP_SPIRAM);
    int count = 0;
    int count_chars = 0;
    struct dirent* in_file = NULL;
    int nr = 0;
    int count_top = 0;
    char prefix[128];
    strcpy(prefix, "");
    
    while (true)
    {
        if (dir != NULL)
            in_file = readdir(dir);
        if (in_file == NULL)
        {
            if (dir) closedir(dir);
            dir = NULL;
            if (count_top==0)
            {
                count_top = count;
                nr = 0;
            } else nr ++;
            if (nr >=count_top)
            {
                break;
            }
            strcpy(prefix, (char*)folders_ref[nr] );
            sprintf(buf, "/sd/romart/%s", prefix);
            printf("DIR: %s\n", buf);
            dir = opendir(buf);
            if (dir)
            {
                strcat(prefix, "/");
            }
            continue;
        }
        if (!strcmp (in_file->d_name, "."))
            continue;
        if (!strcmp (in_file->d_name, ".."))    
            continue;
        if (in_file->d_name[0] == '.')
            continue;
        
        int len = strlen(prefix) + strlen(in_file->d_name) +1;
        if (count_chars+len>MAX_FOLDERS_SIZE || count>=MAX_FOLDERS_COUNT)
        {
            printf("Limit reached!: %d; %d\n", count, count_chars);
            break;
        }
        folders_ref[count] = ((uint32_t)folders_buf) + count_chars;
        strcpy(&folders_buf[count_chars],prefix);
        strcat(&folders_buf[count_chars],in_file->d_name);
        
        char *text = (char*)folders_ref[count];
        
        count++;
        count_chars+=strlen(text)+1;
    }
    if (dir) closedir(dir);
    printf("SIZE: %d\n", sizeof(struct my_folder));
    struct my_folder *rc = (struct my_folder*)heap_caps_malloc(sizeof(struct my_folder), MALLOC_CAP_SPIRAM);
    rc->folders_buf = folders_buf;
    rc->folders_ref = folders_ref;
    rc->count_top = count_top;
    rc->count = count - count_top;
    printf("SIZE: %d\n", sizeof(goemu_util_strings)*rc->count);
    rc->files = (goemu_util_strings*)heap_caps_malloc(sizeof(goemu_util_strings)*rc->count, MALLOC_CAP_SPIRAM);
    int total = 0;
    for (int i = 0;i<rc->count;i++)
    {
        strcpy(prefix, (char*)folders_ref[i+rc->count_top]);
        sprintf(buf, "/sd/romart/%s", prefix);
        goemu_util_strings *f = &(rc->files[i]);
        goemu_util_readdir(f, buf, "art");
        total+=f->count;
    }
    printf("romart count: %d\n", total);
    return rc;
}

void my_free(struct my_folder *s)
{
    heap_caps_free(s->folders_buf);
    heap_caps_free(s->folders_ref);
    for (int i = 0;i < s->count; i++)
    {
        goemu_util_strings_free(&(s->files[i]));
    }
    heap_caps_free(s->files);
    heap_caps_free(s);
}

void droid_ui_idle_run_covers()
{
    ili9341_clear(C_BLACK);
    odroid_ui_draw_chars(8,8, 32, "Initializing folders...", C_WHITE, C_BLACK);
    struct my_folder *files = read_files();
    ili9341_clear(C_BLACK);
    int last_key;
    odroid_gamepad_state joystick;
    int counter = 9999;
    char buf[128];
    #define MAX_LAST_ENTRIES 512
    int last_entries_folder[MAX_LAST_ENTRIES];
    int last_entries_files[MAX_LAST_ENTRIES];
    int entry = -1;
    int entry_max = 0;
    bool draw = false;
    bool pause = false;
    bool choose_rand = false;
    int key_counter = 0;
    int time_to_change = 50*5;
    int first_line_counter = 0;
    #define DRAW_INFO first_line_counter = 100;
    
    while (true)
    {
        IDLE_CHECK_ABORT
        if (first_line_counter>0) first_line_counter--;
        if (first_line_counter==1)
        {
            odroid_ui_draw_chars(0,0, 40, "", C_WHITE, C_BLACK);
        }
        else if (first_line_counter==99)
        {
            sprintf(buf, "[%03d/%03d] delay: %d", entry+1, entry_max, time_to_change * 20);
            odroid_ui_draw_chars(0,0, 40, buf, C_WHITE, C_BLACK);
        }
        if (key_counter>0) key_counter--;
        
        if (joystick.values[ODROID_INPUT_UP] && key_counter == 0)
        {
            time_to_change+=25;
            key_counter = 10;
            DRAW_INFO
        }
        if (joystick.values[ODROID_INPUT_DOWN] && key_counter == 0)
        {
            if (time_to_change>26) time_to_change-=25; 
            key_counter = 10;
            DRAW_INFO
        }
        
        if (joystick.values[ODROID_INPUT_LEFT] && key_counter == 0)
        {
            pause = true;
            if (entry_max == MAX_LAST_ENTRIES)
            {
                entry = (entry-1+MAX_LAST_ENTRIES)%MAX_LAST_ENTRIES;
                draw = true;
            }
            else
            {
            if (entry > 0)
            {
                entry--;
                draw = true;
            }
            }
            key_counter = 10;
            DRAW_INFO
        }
        if (joystick.values[ODROID_INPUT_RIGHT] && key_counter == 0)
        {
            pause = true;
            key_counter = 10;
            if (entry_max == MAX_LAST_ENTRIES)
            {
                entry = (entry+1)%MAX_LAST_ENTRIES;
                draw = true;
            }
            else
            {
            if (entry < entry_max-1)
            {
                entry++;
                draw = true;
            }
            else
            {
                choose_rand = true;
            }
            }
            DRAW_INFO
        }
        if (joystick.values[ODROID_INPUT_SELECT] && key_counter == 0)
        {
            pause = !pause;
            key_counter = 50;
            DRAW_INFO
        }
        
        if (counter++>time_to_change && !pause)
        {
            choose_rand = true;
            counter = 0;
        }
        if (choose_rand)
        {
            choose_rand = false;
            int romart_folder = rand()%files->count;
            goemu_util_strings *s = &(files->files[romart_folder]);
            if (s->count>0)
            {   
                int pic = rand()%s->count;
                entry++;
                if (entry_max<MAX_LAST_ENTRIES) entry_max++;
                if (entry>=MAX_LAST_ENTRIES) entry = 0;
                
                last_entries_folder[entry] = romart_folder;
                last_entries_files[entry] = pic;
                draw = true;
            }
        }
        if (draw)
        {
            draw = false;
            int romart_folder = last_entries_folder[entry];
            int pic = last_entries_files[entry];
            goemu_util_strings *s = &(files->files[romart_folder]);
            char *folder = (char*)files->folders_ref[files->count_top+romart_folder];
            
            sprintf(buf, "/sd/romart/%s/%s", folder, (char*)s->refs[pic]);
            // printf("PIC: %s\n", buf);
            counter = 0;
            uint16_t width, height;
            uint16_t *img = load_pic(buf, &width, &height);
            if (img != NULL)
            {
                int x = (320-width)/2;
                int y = (240-height)/2;
                ili9341_clear(C_BLACK);
                ili9341_write_frame_rectangleLE(x, y, width, height, img);
                heap_caps_free(img);
            }
        }
        
        usleep(20*1000UL);
    }
    my_free(files);
    odroid_ui_wait_for_key(last_key, false);
}

static int get_idle_mode()
{
    if (odroid_ui_idle_mode<0)
    {
        odroid_ui_idle_mode = odroid_settings_int32_get(ODROID_UI_IDLE_MODE_KEY, 0);
        if (odroid_ui_idle_mode >= ODROID_UI_IDLE_MODE_MAX) odroid_ui_idle_mode = 0;
        else if (odroid_ui_idle_mode < 0) odroid_ui_idle_mode = 0;
    }
    return odroid_ui_idle_mode;
}

void odroid_ui_idle_run()
{
    odroid_ui_clean_draw_buffer();
    switch(get_idle_mode())
    {
    case ODROID_UI_IDLE_MODE_CREDITS:
        odroid_ui_idle_run_credits();
    break;
    case ODROID_UI_IDLE_MODE_BLACK:
        odroid_ui_idle_run_black();
    break;
    case ODROID_UI_IDLE_MODE_COVERS:
        droid_ui_idle_run_covers();
    break;
    case ODROID_UI_IDLE_MODE_C64:
        odroid_ui_idle_run_c64_loading();
    break;
    }
    ili9341_clear(C_BLACK);
}

void odroid_ui_func_update_idlemode(odroid_ui_entry *entry) {
    char *text;
    switch(get_idle_mode())
    {
    case ODROID_UI_IDLE_MODE_CREDITS: text = "credits"; break;
    case ODROID_UI_IDLE_MODE_BLACK: text = "black"; break;
    case ODROID_UI_IDLE_MODE_COVERS: text = "covers"; break;
    case ODROID_UI_IDLE_MODE_C64: text = "C64"; break;
    default: text = "n/a"; break;
    }
    sprintf(entry->text, "%-9s: %s", "Info", text);
}

odroid_ui_func_toggle_rc odroid_ui_func_toggle_idlemode(odroid_ui_entry *entry, odroid_gamepad_state *joystick) {
    get_idle_mode();
    if (joystick->values[ODROID_INPUT_RIGHT]) {
        if (++odroid_ui_idle_mode>=ODROID_UI_IDLE_MODE_MAX) odroid_ui_idle_mode = 0;
    } else if (joystick->values[ODROID_INPUT_LEFT]) {
        if (--odroid_ui_idle_mode<0) odroid_ui_idle_mode = ODROID_UI_IDLE_MODE_MAX - 1; 
    }
    odroid_settings_int32_set(ODROID_UI_IDLE_MODE_KEY, odroid_ui_idle_mode);
    if (joystick->values[ODROID_INPUT_A])
    {
        odroid_ui_wait_for_key(ODROID_INPUT_A, false);
        odroid_ui_idle_run();
        return ODROID_UI_FUNC_TOGGLE_RC_MENU_RESTART;
    }
    return ODROID_UI_FUNC_TOGGLE_RC_CHANGED;
}
