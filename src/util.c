#include "util.h"

bool util_exe_path(wchar_t *buf, DWORD cap)
{
    DWORD n = GetModuleFileNameW(NULL, buf, cap);
    return n > 0 && n < cap;
}

bool util_is_elevated(void)
{
    bool elevated = false;
    HANDLE token;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION e;
        DWORD size;
        if (GetTokenInformation(token, TokenElevation, &e, sizeof e, &size))
            elevated = e.TokenIsElevated != 0;
        CloseHandle(token);
    }
    return elevated;
}

bool util_relaunch_elevated(void)
{
    wchar_t path[MAX_PATH];
    if (!util_exe_path(path, MAX_PATH))
        return false;

    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof sei);
    sei.cbSize       = sizeof sei;
    sei.lpVerb       = L"runas";
    sei.lpFile       = path;
    sei.lpParameters = L"--wait";      /* new copy waits for this one to exit */
    sei.nShow        = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

/* SetProcessInformation(ProcessPowerThrottling) is Windows 8+ and not in the
 * Win7-level headers we build against, so it is looked up at run time. */
typedef struct {
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} PowerThrottlingState;

typedef BOOL (WINAPI *SetProcessInformationFn)(HANDLE, int, LPVOID, DWORD);

#define PROCESS_POWER_THROTTLING_INFO_CLASS     4   /* ProcessPowerThrottling */
#define PPT_EXECUTION_SPEED                     0x1
#define PPT_IGNORE_TIMER_RESOLUTION             0x4 /* Windows 11 only */

void util_boost_process(void)
{
    HANDLE self = GetCurrentProcess();

    /* Get CPU time ahead of normal apps, so the hook always answers in time. */
    SetPriorityClass(self, HIGH_PRIORITY_CLASS);

    /* A tray app with no visible window is a prime candidate for EcoQoS /
     * power throttling: low clock, coalesced timers. Opt out explicitly. */
    SetProcessInformationFn set_info = (SetProcessInformationFn)(void (*)(void))
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetProcessInformation");
    if (set_info) {
        PowerThrottlingState s = {
            1, PPT_EXECUTION_SPEED | PPT_IGNORE_TIMER_RESOLUTION, 0  /* 0 = throttling off */
        };
        if (!set_info(self, PROCESS_POWER_THROTTLING_INFO_CLASS, &s, sizeof s)) {
            s.ControlMask = PPT_EXECUTION_SPEED;          /* Windows 10: older flag set */
            set_info(self, PROCESS_POWER_THROTTLING_INFO_CLASS, &s, sizeof s);
        }
    }
}
