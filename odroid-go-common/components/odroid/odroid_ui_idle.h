#pragma once

#include "odroid_ui.h"

void odroid_ui_idle_run();

void odroid_ui_func_update_idlemode(odroid_ui_entry *entry);
odroid_ui_func_toggle_rc odroid_ui_func_toggle_idlemode(odroid_ui_entry *entry, odroid_gamepad_state *joystick);
