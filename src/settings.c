/*
 * Persistence. Everything lives under HKCU, so no admin rights are needed:
 *   HKCU\Software\KeyMouse                              Speed (DWORD)
 *   HKCU\Software\Microsoft\Windows\CurrentVersion\Run  KeyMouse (REG_SZ)
 */
#include "settings.h"
#include "mouse.h"
#include "util.h"

#define SETTINGS_KEY  L"Software\\KeyMouse"
#define RUN_KEY       L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"

Settings g_settings = { 1 };

void settings_load(void)
{
    HKEY key;
    g_settings.speed = 1;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        DWORD type, value, size = sizeof value;
        if (RegQueryValueExW(key, L"Speed", NULL, &type, (BYTE *)&value, &size) == ERROR_SUCCESS &&
            type == REG_DWORD && value < SPEED_COUNT)
            g_settings.speed = (int)value;
        RegCloseKey(key);
    }
}

void settings_save(void)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, NULL, 0, KEY_WRITE, NULL, &key, NULL) == ERROR_SUCCESS) {
        DWORD value = (DWORD)g_settings.speed;
        RegSetValueExW(key, L"Speed", 0, REG_DWORD, (const BYTE *)&value, sizeof value);
        RegCloseKey(key);
    }
}

bool autostart_get(void)
{
    HKEY key;
    bool present = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS) {
        present = RegQueryValueExW(key, APP_NAME, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
        RegCloseKey(key);
    }
    return present;
}

bool autostart_set(bool enable)
{
    HKEY key;
    bool ok = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_WRITE, &key) != ERROR_SUCCESS)
        return false;

    if (enable) {
        wchar_t path[MAX_PATH], cmd[MAX_PATH + 2];
        if (util_exe_path(path, MAX_PATH)) {
            _snwprintf(cmd, MAX_PATH + 2, L"\"%ls\"", path);
            cmd[MAX_PATH + 1] = L'\0';
            ok = RegSetValueExW(key, APP_NAME, 0, REG_SZ, (const BYTE *)cmd,
                                (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
        }
    } else {
        LONG r = RegDeleteValueW(key, APP_NAME);
        ok = (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(key);
    return ok;
}
