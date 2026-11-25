#include "tray.h"
#include "mouse.h"
#include "settings.h"
#include "util.h"

static NOTIFYICONDATAW s_nid;

static HICON load_icon(int id)
{
    return (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(id), IMAGE_ICON,
                             GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                             LR_SHARED);
}

static void fill_state(bool active)
{
    s_nid.hIcon = load_icon(active ? IDI_ACTIVE : IDI_IDLE);
    lstrcpynW(s_nid.szTip,
              active ? L"KeyMouse - ON  (Ctrl+` to turn off)"
                     : L"KeyMouse - off (Ctrl+` to turn on)",
              (int)(sizeof s_nid.szTip / sizeof s_nid.szTip[0]));
}

bool tray_add(HWND hwnd, bool active)
{
    ZeroMemory(&s_nid, sizeof s_nid);
    s_nid.cbSize           = sizeof s_nid;
    s_nid.hWnd             = hwnd;
    s_nid.uID              = 1;
    s_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    s_nid.uCallbackMessage = WM_APP_TRAY;
    fill_state(active);
    return Shell_NotifyIconW(NIM_ADD, &s_nid) != FALSE;
}

void tray_remove(void)
{
    if (s_nid.hWnd)
        Shell_NotifyIconW(NIM_DELETE, &s_nid);
}

void tray_set_active(bool active)
{
    fill_state(active);
    Shell_NotifyIconW(NIM_MODIFY, &s_nid);
}

void tray_show_menu(HWND hwnd, bool active)
{
    const bool elevated = util_is_elevated();
    HMENU menu  = CreatePopupMenu();
    HMENU speed = CreatePopupMenu();

    for (int i = 0; i < SPEED_COUNT; i++)
        AppendMenuW(speed, MF_STRING | (i == g_settings.speed ? MF_CHECKED : 0),
                    IDM_SPEED_FIRST + i, mouse_speed_name(i));

    AppendMenuW(menu, MF_STRING | (active ? MF_CHECKED : 0), IDM_TOGGLE, L"Mouse mode\tCtrl+`");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_POPUP, (UINT_PTR)speed, L"Speed");
    AppendMenuW(menu, MF_STRING | (autostart_get() ? MF_CHECKED : 0), IDM_AUTOSTART, L"Start with Windows");
    AppendMenuW(menu, MF_STRING | (elevated ? MF_CHECKED | MF_GRAYED : 0), IDM_ELEVATE,
                elevated ? L"Running as administrator" : L"Restart as administrator");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);             /* required, or the menu never closes */
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);                     /* destroys the submenu as well */
}
