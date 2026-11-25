#ifndef TRAY_H
#define TRAY_H

#include "common.h"

enum {
    IDM_TOGGLE      = 1001,
    IDM_SPEED_FIRST = 1010,    /* + preset index */
    IDM_AUTOSTART   = 1020,
    IDM_ELEVATE     = 1021,
    IDM_EXIT        = 1099
};

bool tray_add(HWND hwnd, bool active);     /* also used after Explorer restarts */
void tray_remove(void);
void tray_set_active(bool active);
void tray_show_menu(HWND hwnd, bool active);

#endif
