#ifndef COMMON_H
#define COMMON_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

#include "resource.h"

#define APP_NAME   L"KeyMouse"

/*
 * Activation hotkey: Ctrl + <the key under Esc>.
 * We match on the hardware scancode (0x29), not on a virtual-key code, so it is
 * the same physical key on every layout: ` ~ on US, ^ on German, ` on UK, ...
 */
#define HOTKEY_SCANCODE  0x29

/* Private window messages. */
#define WM_APP_TRAY   (WM_APP + 1)   /* tray icon callback                    */
#define WM_APP_STATE  (WM_APP + 2)   /* wParam: 1 = mouse mode on, 0 = off    */

#endif
