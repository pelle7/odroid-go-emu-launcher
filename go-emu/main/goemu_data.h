#pragma once

#include <stdint.h>
#include <string.h>
#include "esp_system.h"
#include "goemu_util.h"

struct goemu_emu_data_entry {
    char partition_name[20];
    char system_name[64];
    char path[32];
    char path_metadata[32];
    char ext[8];
    uint16_t crc_offset;
    bool available;
    uint8_t nr;
    
    uint16_t* image_logo;
    uint16_t* image_header;

    goemu_util_strings files;
    uint32_t *checksums;
    int selected;
    bool initialized;
    
    // initial conifg
    int _selected;
};

typedef struct goemu_emu_data_entry goemu_emu_data_entry;

struct goemu_emu_data {
    goemu_emu_data_entry *entries;
    uint8_t count;
};

typedef struct goemu_emu_data goemu_emu_data;

goemu_emu_data *goemu_data_setup();
goemu_emu_data_entry *goemu_data_get(const char *ext);

void goemu_config_load(goemu_emu_data_entry *emu);
void goemu_config_save_all();
