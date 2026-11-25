#ifndef HOOK_H
#define HOOK_H

#include "common.h"

/* Start the hook thread and install the global low-level keyboard hook on it.
 * 'notify' gets WM_APP_STATE whenever mouse mode is switched on or off. */
bool hook_install(HWND notify);
void hook_uninstall(void);

/* Safe to call from any thread. */
bool hook_is_active(void);
void hook_set_active(bool on);
void hook_toggle(void);

#endif
