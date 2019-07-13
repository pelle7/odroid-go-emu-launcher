#include "goemu_ui.h"
#include "esp_system.h"
#include "esp_event.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/ledc.h"
#include "driver/rtc_io.h"
#include "../components/odroid/odroid_ui.h"
#include "../components/odroid/odroid_system.h"
#include "../components/odroid/odroid_display.h"
#include "../components/odroid/odroid_input.h"
#include "../components/odroid/odroid_audio.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>

#define ROM_PATH "/sd/roms"

void goemu_ui_choose_file_init(goemu_emu_data_entry *emu) {
    if (emu->initialized)
    {
        return;
    }
    emu->initialized = true;
    if (emu->files.count == 0)
    {
        char buf[128];
        sprintf(buf, "%s/%s", ROM_PATH, emu->path);
        goemu_util_readdir(&emu->files, buf, emu->ext);
        goemu_util_strings_sort(&emu->files);
        if (emu->files.count > 0)
        {
            emu->checksums = (uint32_t*)heap_caps_malloc(emu->files.count*4, MALLOC_CAP_SPIRAM);
            memset(emu->checksums, 0, emu->files.count*4);
        }
    }
    goemu_config_load(emu);

    if (emu->selected >= emu->files.count)
    {
        emu->selected = 0;
    }
    
    int count = emu->files.count;
    uint32_t *entries_refs = emu->files.refs;

    clean_draw_buffer();
    odroid_gamepad_state lastJoysticState;
    odroid_input_gamepad_read(&lastJoysticState);
    
    if (emu->selected == 0)
    {
        char *selected_file = odroid_settings_RomFilePath_get();
        if (selected_file) {
            if (strlen(selected_file) <strlen(emu->ext)+1 ||
                strcasecmp(emu->ext, &selected_file[strlen(selected_file)-strlen(emu->ext)])!=0 ) {
               printf("odroid_ui_choose_file: Ignoring last file: '%s'\n", selected_file);
               free(selected_file);
               selected_file = NULL;
            } else {
                // Found a match
                   for (int i = 0;i < count; i++) {
                      char *file = (char*)entries_refs[i];
                      if (strlen(selected_file) < strlen(file)) continue;
                      char *file2 = &selected_file[strlen(selected_file)-strlen(file)];
                      if (strcmp(file, file2) == 0) {
                          emu->selected = i;
                          printf("Last selected: %d: '%s'\n", emu->selected, file);
                          break;
                      }
                   }
                   free(selected_file);
                   selected_file = NULL;
            }
        }
    }
}

char *goemu_ui_choose_file_getfile(goemu_emu_data_entry *emu)
{
    char *file = (char*)emu->files.refs[emu->selected];
    char *rc = (char*)malloc(strlen(emu->path) + 1+ strlen(file)+1+strlen(ROM_PATH)+1);
    sprintf(rc, "%s/%s/%s", ROM_PATH, emu->path, file);
    return rc;
}

void goemu_ui_choose_file_input(goemu_emu_data_entry *emu, odroid_gamepad_state *joystick, int *last_key_)
{
    int last_key = *last_key_;
    int selected = emu->selected;
    if (joystick->values[ODROID_INPUT_B]) {
        last_key = ODROID_INPUT_B;
            //entry_rc = ODROID_UI_FUNC_TOGGLE_RC_MENU_CLOSE;
    } else if (joystick->values[ODROID_INPUT_VOLUME]) {
        last_key = ODROID_INPUT_VOLUME;
           // entry_rc = ODROID_UI_FUNC_TOGGLE_RC_MENU_CLOSE;
    } else if (joystick->values[ODROID_INPUT_UP]) {
            last_key = ODROID_INPUT_UP;
            selected--;
            if (selected<0) selected = emu->files.count - 1;
    } else if (joystick->values[ODROID_INPUT_DOWN]) {
            last_key = ODROID_INPUT_DOWN;
            selected++;
            if (selected>=emu->files.count) selected = 0;
    } else if (joystick->values[ODROID_INPUT_LEFT]) {
        last_key = ODROID_INPUT_LEFT;
        char st = ((char*)emu->files.refs[selected])[0];
        int max = 20;
        while (selected>0 && max-->0)
        {
           selected--;
           if (st != ((char*)emu->files.refs[selected])[0]) break;
        }
        //selected-=10;
        //if (selected<0) selected = 0;
    } else if (joystick->values[ODROID_INPUT_RIGHT]) {
        last_key = ODROID_INPUT_RIGHT;
        char st = ((char*)emu->files.refs[selected])[0];
        int max = 20;
        while (selected<emu->files.count-1 && max-->0)
        {
           selected++;
           if (st != ((char*)emu->files.refs[selected])[0]) break;
        }
        //selected+=10;
        //if (selected>=emu->files.count) selected = emu->files.count - 1;
    }
    emu->selected = selected;
    *last_key_ = last_key;
}

void goemu_ui_choose_file_draw(goemu_emu_data_entry *emu)
{
    int count = emu->files.count;
    uint32_t *entries_refs = emu->files.refs;
    int x = 0;
    int lines = 30 - 7;
    int y_offset = 7*8;
    
    for (int i = 0;i < lines; i++) {
        int y = y_offset + i * 8;
        int entry = emu->selected + i - lines/2;
        char *text;
        if (entry>=0 && entry < count)
        {
            text = (char*)entries_refs[entry];
        } else
        {
            text = " ";
        }
        draw_chars(x, y, 40, text, entry==emu->selected?color_selected:color_default, color_bg_default);
    }
        
}

void goemu_ui_choose_file_load(goemu_emu_data_entry *emu)
{
    if (emu->image_logo == NULL)
    {
        char file[128];
        sprintf(file, "/sd/odroid/metadata/%s/logo.raw", emu->path_metadata);
        FILE *f = fopen(file,"rb");
        if (f)
        {
            emu->image_logo = (uint16_t*)heap_caps_malloc(GOEMU_IMAGE_LOGO_WIDTH*GOEMU_IMAGE_LOGO_HEIGHT*2, MALLOC_CAP_SPIRAM);
            fread(emu->image_logo, sizeof(uint16_t), GOEMU_IMAGE_LOGO_WIDTH*GOEMU_IMAGE_LOGO_HEIGHT, f);
            fclose(f);
        }
        else
        {
            printf("Image Logo '%s' not found\n", file);
        }
    }
    
    if (emu->image_header == NULL)
    {
        char file[128];
        sprintf(file, "/sd/odroid/metadata/%s/header.raw", emu->path_metadata);
        FILE *f = fopen(file,"rb");
        if (f)
        {
            emu->image_header = (uint16_t*)heap_caps_malloc(GOEMU_IMAGE_HEADER_WIDTH*GOEMU_IMAGE_HEADER_HEIGHT*2, MALLOC_CAP_SPIRAM);
            fread(emu->image_header, sizeof(uint16_t), GOEMU_IMAGE_HEADER_WIDTH*GOEMU_IMAGE_HEADER_HEIGHT, f);
            fclose(f);
        }
        else
        {
            printf("Image Header '%s' not found\n", file);
        }
    }
}