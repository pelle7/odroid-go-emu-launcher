#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_partition.h"
#include "driver/i2s.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "esp_ota_ops.h"
#include "rom/crc.h"

#include "../components/odroid/odroid_settings.h"
#include "../components/odroid/odroid_audio.h"
#include "../components/odroid/odroid_input.h"
#include "../components/odroid/odroid_system.h"
#include "../components/odroid/odroid_display.h"
#include "../components/odroid/odroid_sdcard.h"
#include "../components/odroid/odroid_ui.h"
#include "../components/odroid/odroid_ui_idle.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "goemu_data.h"
#include "goemu_ui.h"

const char* SD_BASE_PATH = "/sd";

void odroid_setup()
{
    nvs_flash_init();

    odroid_system_init();

    // Joystick.
    odroid_input_gamepad_init();
    odroid_input_battery_level_init();
    
    ili9341_prepare();
    
    // Audio
    odroid_volume_level level = odroid_settings_Volume_get();
    odroid_audio_volume_set(level);


    // Disable LCD CD to prevent garbage
    const gpio_num_t LCD_PIN_NUM_CS = GPIO_NUM_5;

    gpio_config_t io_conf = { 0 };
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << LCD_PIN_NUM_CS);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;

    gpio_config(&io_conf);
    gpio_set_level(LCD_PIN_NUM_CS, 1);

    esp_sleep_source_t cause = esp_sleep_get_wakeup_cause();
    switch (cause)
    {
        case ESP_SLEEP_WAKEUP_EXT0:
        {
            printf("app_main: ESP_SLEEP_WAKEUP_EXT0 deep sleep reset\n");
            break;
        }

        case ESP_SLEEP_WAKEUP_EXT1:
        case ESP_SLEEP_WAKEUP_TIMER:
        case ESP_SLEEP_WAKEUP_TOUCHPAD:
        case ESP_SLEEP_WAKEUP_ULP:
        case ESP_SLEEP_WAKEUP_UNDEFINED:
        {
            printf("app_main: Unexpected deep sleep reset: %d\n", cause);

            odroid_gamepad_state bootState = odroid_input_read_raw();

            if (bootState.values[ODROID_INPUT_MENU])
            {
                printf("MENU pressed on startup\n");
            }

            if (bootState.values[ODROID_INPUT_START])
            {
                printf("START pressed on startup\n");
            }
        }
            break;

        default:
            printf("app_main: Not a deep sleep reset\n");
            break;
    }

    ili9341_init();
    
    esp_err_t r = odroid_sdcard_open(SD_BASE_PATH);
    if (r != ESP_OK)
    {
        odroid_display_show_sderr(ODROID_SD_ERR_NOCARD);
        abort();
    }
}

void goemu_start(const char *emu_label)
{
    esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, emu_label);
    if (partition)
    {
        printf("EMU found: %s; address: %X\n", emu_label, partition->address);
    }
    else
    {
        printf("EMU not found: %s\n", emu_label);
        return;
    }
    odroid_settings_ForceInternalGameSelect_set(0);
    goemu_config_save_all();

    esp_err_t err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK)
    {
        printf("odroid_system_application_set: esp_ota_set_boot_partition failed.\n");
        abort();
    }
    esp_restart();
}

void draw_cover(goemu_emu_data_entry *emu, odroid_gamepad_state *joystick)
{
    if (emu->files.count == 0)
    {
        odroid_ui_draw_chars(320-8*12, (6)*8, 12, " ", C_RED, C_BLACK);
        return;
    }
    uint32_t crc = emu->checksums[emu->selected];
    if (crc == 0)
    {
        odroid_ui_draw_chars(320-8*12, (6)*8, 12, "       CRC32", C_GREEN, C_BLACK);
        char *file = goemu_ui_choose_file_getfile(emu);
        FILE *f = fopen(file, "rb");
        if (f)
        {
            fseek(f, 0, SEEK_END);
            int size = ftell(f) - emu->crc_offset;
            fseek(f, emu->crc_offset, SEEK_SET);
            int buf_size = 32768; //4096;
            unsigned char *buffer = (unsigned char*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
            if (buffer)
            {
                uint32_t crc_tmp = 0;
                bool abort = false;
                while (true)
                {
                    odroid_input_gamepad_read(joystick);
                    if (joystick->values[ODROID_INPUT_A] ||
                        joystick->values[ODROID_INPUT_START] ||
                        joystick->values[ODROID_INPUT_SELECT] ||
                        joystick->values[ODROID_INPUT_LEFT] ||
                        joystick->values[ODROID_INPUT_RIGHT] ||
                        joystick->values[ODROID_INPUT_UP] ||
                        joystick->values[ODROID_INPUT_DOWN]) {
                        abort = true;
                        break;
                    }
                    int count = fread(buffer, 1, buf_size, f);
                    crc_tmp = crc32_le(crc_tmp, buffer, count);
                    if (count != buf_size)
                    {
                        break;
                    }
                }
                heap_caps_free(buffer);
                fclose(f);
                if (!abort)
                {
                    // printf("%X  %s ; Size: %d\n", crc_tmp, file, size);
                    crc = crc_tmp;
                }
            } else
            {
                printf("Buffer alloc failed: Size: %d; File: %s\n",size, file);
                fclose(f);
                crc = 1;
            }
        } else
        {
            printf("File not found: %s\n", file);
            crc = 1;
        }
        free(file);
    }
    if (crc > 0)
    {
        char buf[128], buf_crc[10];
        sprintf(buf_crc, "%X", crc);
        sprintf(buf, "/sd/romart/%s/%c/%s.art", emu->path_metadata, buf_crc[0], buf_crc);
        FILE *f = fopen(buf, "rb");
        if (f)
        {
            uint16_t width, height;
            fread(&width, 2, 1, f);
            fread(&height, 2, 1, f);
            if (width<=320 && height<=176)
            {
                // printf("%X  Romart found: %s   (%dx%d)\n", crc, buf, width, height);
                uint16_t *img = (uint16_t*)heap_caps_malloc(width*height*2, MALLOC_CAP_SPIRAM);
                fread(img, 2, width*height, f);
                
                ili9341_write_frame_rectangleLE(320-width,240-height, width, height, img);
                heap_caps_free(img);
            }
            else
            {
                // printf("%X  Romart found: %s   (%dx%d) (INVALID SIZE)\n", crc, buf, width, height);
                crc = 1;
            }
            fclose(f);
        }
        else
        {
            // printf("%X  Romart not found: %s\n", crc, buf);
            crc = 1;
        }
    }
    if (crc == 1)
    {
        odroid_ui_draw_chars(320-8*12, (6)*8, 12, "No art found", C_RED, C_BLACK);
    } else
    {
        odroid_ui_draw_chars(320-8*12, (6)*8, 12, " ", C_RED, C_BLACK);
    }
    emu->checksums[emu->selected] = crc;
}

void odroid_goemu_menu(odroid_ui_window *window)
{
    odroid_ui_create_entry(window, &odroid_ui_func_update_idlemode, &odroid_ui_func_toggle_idlemode);
}

void goemu_loop()
{
    goemu_emu_data *all_emus = goemu_data_setup();
    int repeat = 0;
    int last_key = -1;
    int selected_last = -1;
    int selected_emu_last = -1;
    int selected_emu = 0;
    int idle_counter = 0;
    goemu_emu_data_entry *emu = NULL;
    
    char *selected_file = odroid_settings_RomFilePath_get();
    if (selected_file)
    {
        int i;
        for ( i = strlen(selected_file) - 1; i>=0;i--)
        {
            if (selected_file[i] == '.') break;
        }
        free(selected_file);
        if (i > 0)
        {
            char *ext = &selected_file[i+1];
            for ( i = 0; i < all_emus->count; i++)
            {
                goemu_emu_data_entry *p = &all_emus->entries[i];
                if (strcmp(p->ext, ext)==0)
                {
                    selected_emu = i;
                }
            }
        }
    }
    
    odroid_display_lock();
    
    int battery_counter = 1000;
    int battery_percentage_old = 0; 
    odroid_battery_state battery_state;
    bool battery_draw = false;
    odroid_input_battery_level_read(&battery_state);
    bool menu_restart = false;

//#define DEBUG_TEST_BATTERY
    
    while (true)
    {
#ifdef DEBUG_TEST_BATTERY
        if (battery_counter++>=20)
        {
            if (++battery_state.percentage>100) battery_state.percentage = 0;
#else
        if (battery_counter++>=100)
        {
            odroid_input_battery_level_read(&battery_state);
#endif
            printf("HEAP:0x%x, BATTERY:%d [%d]\n", esp_get_free_heap_size(), battery_state.millivolts, battery_state.percentage);
            battery_counter = 0;
            int tmp = ((battery_state.percentage-1)/5) + 1;
            if (battery_percentage_old != tmp)
            {
                battery_percentage_old = tmp;
                battery_draw = true;
            }  
        } else if (battery_percentage_old<3 && (battery_counter%3==0)) battery_draw = true;
        
        if (selected_emu != selected_emu_last)
        {
            if (selected_emu >= all_emus->count)
            {
                selected_emu = 0;
            } else if (selected_emu < 0)
            {
                selected_emu = all_emus->count - 1;
            }
            goemu_emu_data_entry *emu_last = emu;
            emu = &all_emus->entries[selected_emu];
            int y = 4;
            int x = 6 * 8;
            int length = 40 - 6;
            odroid_ui_draw_chars(x, y*8, length, emu->system_name, color_selected, C_BLACK);
            bool first = !emu->initialized;
            if (first)
            {
                odroid_ui_draw_chars(x, (y+1)*8, length, "Loading directory...", color_selected, C_BLACK);
                // Workaround for 4GB sd card? If the last SD-card access failed, new one fails too? 
                if (emu_last && emu_last->files.count>0)
                {
                    char *file = goemu_ui_choose_file_getfile(emu_last);
                    FILE *f = fopen(file, "rb");
                    if (f) fclose(f);
                }
            }
            goemu_ui_choose_file_load(emu);
            if (emu->image_logo)
            {
                ili9341_write_frame_rectangleLE(0,0, GOEMU_IMAGE_LOGO_WIDTH,GOEMU_IMAGE_LOGO_HEIGHT, emu->image_logo);
            }
            if (emu->image_header)
            {
                ili9341_write_frame_rectangleLE(48,0, GOEMU_IMAGE_HEADER_WIDTH, GOEMU_IMAGE_HEADER_HEIGHT, emu->image_header);
            }            
            goemu_ui_choose_file_init(emu);
            selected_emu_last = selected_emu;
            selected_last = -1;
            if (first)
            {
                odroid_ui_draw_chars(x, (y+1)*8, length, " ", color_selected, C_BLACK);
            }
            char buf[40];
            if (emu->available)
            {
                sprintf(buf, "Games: %d", emu->files.count);
            }
            else
            {
                sprintf(buf, "Games: %d - EMU not found '%s'", emu->files.count, emu->partition_name);
            }
            odroid_ui_draw_chars(x, (y+1)*8, length, buf, color_selected, C_BLACK);
            idle_counter = 0;
            battery_draw = true;
        }
        if (battery_draw)
        {
            odroid_ui_battery_draw(320 - 23 ,1, 20, battery_percentage_old);
            battery_draw = false;
        }
        odroid_gamepad_state joystick;
        odroid_input_gamepad_read(&joystick);
        
        if (joystick.values[last_key]) {
            idle_counter = 0;
        }
        
        if (idle_counter++==6)
        {
            draw_cover(emu, &joystick);
        }
        
        if (idle_counter> 50*60)
        {
            odroid_display_unlock();
            odroid_ui_idle_run();
            odroid_display_lock();
            selected_emu_last = -1;
            selected_last = -1;
            idle_counter = 0;
        }
        
        if (last_key >= 0) {
            if (!joystick.values[last_key]) {
                last_key = -1;
                repeat = 0;
            } else if (repeat++>6) {
             repeat = 6;
             last_key = -1;
         }
        } else {
            if (joystick.values[ODROID_INPUT_A]) {
                last_key = ODROID_INPUT_A;
                if (emu->available)
                {
                    break;
                }
                else
                {
                    odroid_ui_error(" Emulator is not installed! ");
                }
            } else if (joystick.values[ODROID_INPUT_START]) {
                last_key = ODROID_INPUT_START;
                if (emu->available)
                {
                    int sel = odroid_ui_ask_v2("Load savestate?",color_selected, color_bg_default, 1);
                    if (sel < 0) 
                    {
                        selected_last = -1;
                        idle_counter = 0;
                        continue;
                    } else if (sel==1)
                    {
                        odroid_settings_StartAction_set(ODROID_START_ACTION_RESTART);
                    }
                    break;
                }
                else
                {
                    odroid_ui_error(" Emulator is not installed! ");
                }
            } else if (joystick.values[ODROID_INPUT_SELECT]) {
                last_key = ODROID_INPUT_SELECT;
                selected_emu++;
                repeat = 0;
            } else if (joystick.values[ODROID_INPUT_START]) {
                last_key = ODROID_INPUT_START;
            }
            else if (joystick.values[ODROID_INPUT_MENU]) {
                esp_restart();
            } else if (joystick.values[ODROID_INPUT_VOLUME] || menu_restart) {
                last_key = ODROID_INPUT_VOLUME;
                odroid_display_unlock();
                menu_restart = odroid_ui_menu_ext(menu_restart, &odroid_goemu_menu);
                odroid_display_lock();
                selected_last = -1;
                idle_counter = 0;
                if (menu_restart)
                {
                    selected_emu_last = - 1;
                }
            } else
            {
            goemu_ui_choose_file_input(emu, &joystick, &last_key);
            }
        }
        if (selected_last != emu->selected)
        {
            goemu_ui_choose_file_draw(emu);
            //if (emu->files.count > 0 && emu->checksums[emu->selected] > 1)
        }
        
        usleep(20*1000UL);
        selected_last = emu->selected;
    }    
    odroid_ui_wait_for_key(last_key, false);
    odroid_display_unlock();
    ili9341_blank_screen();
    
    char *rc = goemu_ui_choose_file_getfile(emu);
    printf("Selected game: %s\n", rc);
    if (rc)
    {
        odroid_settings_RomFilePath_set(rc);
        goemu_start(emu->partition_name);
    }
}

void goemu_setup()
{
    const char *emu_label = "smsplusgx";
    esp_partition_t *rc = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, emu_label);
    if (rc)
    {
        printf("EMU found: %s; address: %X\n", emu_label, rc->address);
    }
    else
    {
        printf("EMU not found: %s\n", emu_label);
    }
}

void app_main(void)
{
    printf("go-emu (%s-%s).\n", COMPILEDATE, GITREV);
    
    odroid_setup();
    odroid_ui_enter_loop();
    ili9341_blank_screen();
    goemu_setup();
    goemu_loop();
    odroid_ui_ask("go-emu!");            
}
