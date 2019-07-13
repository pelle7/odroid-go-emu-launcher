#pragma once

#include "esp_system.h"
#include <stdio.h>
#include <string.h>

struct goemu_util_strings {
    uint32_t *refs;
    char *buffer;
    int count;
};

typedef struct goemu_util_strings goemu_util_strings;

void goemu_util_readdir(goemu_util_strings *rc, const char *path, const char *ext);
void goemu_util_strings_sort(goemu_util_strings *data);
void goemu_util_strings_free(goemu_util_strings *data);
