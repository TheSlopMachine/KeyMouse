#ifndef SETTINGS_H
#define SETTINGS_H

#include "common.h"

typedef struct {
    int speed;                 /* index into the speed presets (see mouse.c) */
} Settings;

extern Settings g_settings;

void settings_load(void);
void settings_save(void);

bool autostart_get(void);
bool autostart_set(bool enable);

#endif
