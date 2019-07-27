#include "goemu_data.h"
#include "esp_partition.h"
#include "../components/odroid/odroid_settings.h"
#include <dirent.h>

static const char* NvsKey_EmuOffset = "EmuOffset";

goemu_emu_data *goemu_data;

goemu_emu_data_entry *goemu_data_add(goemu_emu_data *d, const char *system, const char *path, const char* ext, const char *part)
{
    goemu_emu_data_entry *p = &d->entries[d->count];
    p->nr = d->count;
    strcpy(p->partition_name, part);
    strcpy(p->system_name, system);
    strcpy(p->path, path);
    strcpy(p->path_metadata, path);
    strcpy(p->ext, ext);
    p->files.count = 0;
    p->files.buffer = NULL;
    p->files.refs = NULL;
    p->selected = 0;
    p->available = false;
    p->image_logo = NULL;
    p->image_header = NULL;
    p->initialized = false;
    p->checksums = NULL;
    p->crc_offset = 0;
    d->count++;
    return p;
}

goemu_emu_data *goemu_data_setup()
{
    uint8_t max = 12;
    goemu_emu_data *rc = (goemu_emu_data*)malloc(sizeof(goemu_emu_data));
    rc->entries = (goemu_emu_data_entry*)malloc(sizeof(goemu_emu_data_entry)*max);
    rc->count = 0;
    goemu_emu_data_entry *p;
    p = goemu_data_add(rc, "Nintendo Entertainment System", "nes", "nes", "nesemu");
    p->crc_offset = 16;
    goemu_data_add(rc, "Nintendo Gameboy", "gb", "gb", "gnuboy");
    goemu_data_add(rc, "Nintendo Gameboy Color", "gbc", "gbc", "gnuboy");
    goemu_data_add(rc, "Sega Master System", "sms", "sms", "smsplusgx");
    goemu_data_add(rc, "Sega Game Gear", "gg", "gg", "smsplusgx");
    goemu_data_add(rc, "ColecoVision", "col", "col", "smsplusgx");
    p = goemu_data_add(rc, "Atari Lynx", "lnx", "lnx", "lynx");
    {
        DIR* dir = opendir("/sd/roms/lnx");
        if (dir)
        {
            closedir(dir);
        }
        else
        {
            strcpy(p->path, "lynx");
        }
    }
    goemu_data_add(rc, "Atari 2600", "a26", "a26", "stella");
    goemu_data_add(rc, "Atari 7800", "a78", "a78", "prosystem");
    goemu_data_add(rc, "ZX Spectrum", "spectrum", "z80", "spectrum");
    
    goemu_data = rc;
    for (int i = 0; i < goemu_data->count; i++)
    {
        goemu_emu_data_entry *p = &goemu_data->entries[i];
        esp_partition_t *partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, p->partition_name);
        p->available = partition!=NULL;
    }
    return rc;
}

goemu_emu_data_entry *goemu_data_get(const char *ext)
{
    for (int i = 0; i < goemu_data->count; i++)
    {
        goemu_emu_data_entry *p = &goemu_data->entries[i];
        if (strcmp(p->ext, ext)==0)
        {
            return p;
        }
    }
    return NULL;
}

void goemu_config_load(goemu_emu_data_entry *emu)
{
    char key[32];
    sprintf(key, "%s%d", NvsKey_EmuOffset, emu->nr);
    int32_t sel = odroid_settings_int32_get(key, -1);
    if (sel != -1) emu->selected = sel;
    
    emu->_selected = emu->selected;
    // printf("CONFIG: LOAD: %s: selected: %d\n", key, emu->selected);
}

void goemu_config_save_all()
{
    char key[32];
    for (int i = 0; i < goemu_data->count; i++)
    {
        goemu_emu_data_entry *emu = &goemu_data->entries[i];
        if (!emu->initialized || emu->_selected == emu->selected)
        {
            continue;
        }
        sprintf(key, "%s%d", NvsKey_EmuOffset, emu->nr);
        // printf("CONFIG: SAVE: %s: selected: %d\n", key, emu->selected);
        odroid_settings_int32_set(key, emu->selected);
    }
}
