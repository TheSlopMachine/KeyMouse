/*
 * KeyMouse - drive the mouse pointer from the keyboard.
 *
 *   Ctrl + `(key under Esc)   toggle mouse mode
 *   Arrow keys                move the pointer (hold to accelerate)
 *   Enter                     left button   (hold = drag)
 *   Shift + Enter             right button
 *
 * main.c owns the hidden window, the message loop and the glue between the
 * tray, the keyboard hook and the mouse module.
 */
#include "common.h"
#include "hook.h"
#include "mouse.h"
#include "settings.h"
#include "tray.h"
#include "util.h"

#define WND_CLASS   L"KeyMouseHiddenWindow"
#define MUTEX_NAME  L"Local\\KeyMouse.SingleInstance"

static UINT s_taskbar_created;

static bool acquire_single_instance(bool wait_for_previous)
{
    HANDLE m = CreateMutexW(NULL, TRUE, MUTEX_NAME);   /* owned if we created it */
    if (!m)
        return false;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        /* After "Restart as administrator" the old copy is still shutting down. */
        DWORD w = WaitForSingleObject(m, wait_for_previous ? 5000 : 0);
        return w == WAIT_OBJECT_0 || w == WAIT_ABANDONED;
    }
    return true;                           /* handle intentionally kept for process lifetime */
}

static void on_command(HWND hwnd, UINT id)
{
    if (id == IDM_TOGGLE) {
        hook_toggle();
    } else if (id >= IDM_SPEED_FIRST && id < IDM_SPEED_FIRST + SPEED_COUNT) {
        g_settings.speed = (int)(id - IDM_SPEED_FIRST);
        mouse_set_speed(g_settings.speed);
        settings_save();
    } else if (id == IDM_AUTOSTART) {
        autostart_set(!autostart_get());
    } else if (id == IDM_ELEVATE) {
        if (util_relaunch_elevated())
            DestroyWindow(hwnd);
    } else if (id == IDM_EXIT) {
        DestroyWindow(hwnd);
    }
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (s_taskbar_created && msg == s_taskbar_created) {   /* Explorer restarted */
        tray_add(hwnd, hook_is_active());
        return 0;
    }

    switch (msg) {
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONUP:
            hook_toggle();
            break;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            tray_show_menu(hwnd, hook_is_active());
            break;
        }
        return 0;

    case WM_APP_STATE:
        tray_set_active(wp != 0);
        return 0;

    case WM_COMMAND:
        on_command(hwnd, LOWORD(wp));
        return 0;

    case WM_DESTROY:
        hook_uninstall();                  /* stops the hook thread         */
        mouse_shutdown();                  /* releases any held button      */
        tray_remove();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    (void)prev; (void)cmdline; (void)show;

    const bool wait = wcsstr(GetCommandLineW(), L"--wait") != NULL;
    if (!acquire_single_instance(wait))
        return 0;                          /* already running */

    util_boost_process();                  /* high priority, no EcoQoS      */

    settings_load();
    mouse_set_speed(g_settings.speed);

    /* A hidden top-level window (not message-only: message-only windows do not
     * receive the "TaskbarCreated" broadcast we need to restore the tray icon). */
    s_taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof wc);
    wc.cbSize        = sizeof wc;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = inst;
    wc.hIcon         = LoadIconW(inst, MAKEINTRESOURCEW(IDI_APP));
    wc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = WND_CLASS;
    if (!RegisterClassExW(&wc))
        return 1;

    HWND hwnd = CreateWindowExW(0, WND_CLASS, APP_NAME, WS_OVERLAPPED,
                                0, 0, 0, 0, NULL, NULL, inst, NULL);
    if (!hwnd)
        return 1;

    if (!mouse_init() || !hook_install(hwnd)) {
        MessageBoxW(NULL, L"Could not install the keyboard hook.", APP_NAME, MB_ICONERROR);
        return 1;
    }
    tray_add(hwnd, false);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return (int)m.wParam;
}
