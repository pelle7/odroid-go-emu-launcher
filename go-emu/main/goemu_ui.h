#pragma once

#include "goemu_data.h"
#include "../components/odroid/odroid_input.h"

#define GOEMU_IMAGE_LOGO_WIDTH  47 
#define GOEMU_IMAGE_LOGO_HEIGHT 51

#define GOEMU_IMAGE_HEADER_WIDTH  272 
#define GOEMU_IMAGE_HEADER_HEIGHT 32

struct goemu_emu_ui {
    goemu_emu_data_entry *emu;
    int selected;
};

typedef struct goemu_emu_data_entry goemu_emu_data_entry;

void goemu_ui_choose_file_load(goemu_emu_data_entry *emu);
void goemu_ui_choose_file_init(goemu_emu_data_entry *emu);
void goemu_ui_choose_file_input(goemu_emu_data_entry *emu, odroid_gamepad_state *joystick, int *last_key);
void goemu_ui_choose_file_draw(goemu_emu_data_entry *emu);
char *goemu_ui_choose_file_getfile(goemu_emu_data_entry *emu);
