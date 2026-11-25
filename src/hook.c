/*
 * Global keyboard hook and the mouse-mode state machine.
 *
 * A WH_KEYBOARD_LL hook sees every key press *before* it reaches any
 * application, and returning non-zero from it discards the event. That is what
 * lets us eat the arrow keys / Enter while mouse mode is on.
 *
 * Why the hook has its own thread:
 *   Windows calls an LL hook by sending a message to the thread that installed
 *   it and waits at most LowLevelHooksTimeout (~300-1000 ms) for the answer.
 *   If the thread is busy or not scheduled in time, the key is passed on to the
 *   application anyway (the "leak"), and after a few timeouts Windows silently
 *   removes the hook altogether. So:
 *     - this thread does nothing but run the hook, at TIME_CRITICAL priority;
 *     - the callback only flips flags and posts commands to the mouse thread
 *       (no SendInput / SetCursorPos / UI in here);
 *     - a watchdog reinstalls the hook if it stops being called, and every
 *       activation reinstalls it so we are first in the hook chain (the most
 *       recently installed LL hook is called first).
 *
 * Rules:
 *   - Only keys we actually use are swallowed. Everything else (letters,
 *     Shift, Ctrl, Alt+Tab, ...) passes through untouched.
 *   - A key-up is swallowed only if we swallowed the matching key-down, so
 *     apps never see a stray release (and we never miss one).
 *   - Injected keys are handled like physical ones: while mouse mode is on,
 *     nothing else gets arrows/Enter, even if a remapper re-injects them.
 *     (We inject only mouse events, so there is no feedback loop.)
 */
#include "hook.h"
#include "mouse.h"

enum {
    HM_SET_ACTIVE = WM_APP + 10,   /* wParam: 0 off, 1 on, 2 toggle */
    HM_REINSTALL
};

#define WATCHDOG_MS  1000          /* how often the watchdog looks           */
#define STALE_MS     500           /* input newer than last hook call by this */

static HWND           s_notify;
static HANDLE         s_thread;
static DWORD          s_tid;
static volatile LONG  s_installed;
static volatile LONG  s_active_pub;    /* readable from any thread */

/* --- everything below is touched only by the hook thread ---------------- */
static HHOOK  s_hook;
static DWORD  s_last_call;             /* GetTickCount() of the last callback */
static bool   s_active;
static bool   s_hotkey_held;           /* activation key is down (and was eaten) */
static bool   s_arrow_held[DIR_COUNT]; /* arrow down (and was eaten)             */
static bool   s_enter_held;            /* Enter down (and was eaten)             */
static Button s_enter_button;          /* which button this Enter press owns     */

static LRESULT CALLBACK ll_proc(int code, WPARAM wp, LPARAM lp);

static bool key_down(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

/* Install a fresh hook (goes to the head of the chain), then drop the old one.
 * Runs between message pumps, so no callback can be in flight here. */
static bool install(void)
{
    HHOOK h = SetWindowsHookExW(WH_KEYBOARD_LL, ll_proc, GetModuleHandleW(NULL), 0);
    if (!h)
        return false;
    if (s_hook)
        UnhookWindowsHookEx(s_hook);
    s_hook      = h;
    s_last_call = GetTickCount();
    return true;
}

/* Windows never tells us it removed a timed-out hook. If there was input
 * after our last callback, assume the worst and reinstall (cheap, harmless). */
static void watchdog(void)
{
    LASTINPUTINFO li;
    li.cbSize = sizeof li;
    if (GetLastInputInfo(&li) && (LONG)(li.dwTime - s_last_call) > STALE_MS)
        install();
}

static void set_active(bool on)
{
    if (s_active == on)
        return;
    s_active = on;
    InterlockedExchange(&s_active_pub, on);
    if (on) {
        PostThreadMessageW(s_tid, HM_REINSTALL, 0, 0);   /* be first in the chain */
    } else {
        /* Stop moving / release buttons. The held flags stay, so the matching
         * key-ups are still swallowed and apps never see a stray release. */
        mouse_release_all();
    }
    /* Tray update is deferred: keep the hook callback fast. */
    PostMessageW(s_notify, WM_APP_STATE, on ? 1 : 0, 0);
}

static bool on_arrow(Dir d, bool down)
{
    if (down) {
        s_arrow_held[d] = true;
        mouse_dir_press(d);                /* mouse thread ignores auto-repeat */
        return true;
    }
    if (s_arrow_held[d]) {
        s_arrow_held[d] = false;
        mouse_dir_release(d);
        return true;
    }
    return false;                          /* pressed before activation: let it through */
}

static bool on_enter(bool down)
{
    if (down) {
        if (!s_enter_held) {               /* first press, not auto-repeat */
            s_enter_held   = true;
            s_enter_button = key_down(VK_SHIFT) ? BTN_RIGHT : BTN_LEFT;
            mouse_button(s_enter_button, true);
        }
        return true;
    }
    if (s_enter_held) {
        s_enter_held = false;
        mouse_button(s_enter_button, false);   /* release the one we pressed, even if Shift is gone */
        return true;
    }
    return false;
}

/* Returns true if the event must be swallowed. */
static bool handle_key(const KBDLLHOOKSTRUCT *k)
{
    const bool down = !(k->flags & LLKHF_UP);

    /* --- activation hotkey: Ctrl + key under Esc ------------------------- */
    if (k->scanCode == HOTKEY_SCANCODE && !(k->flags & LLKHF_EXTENDED)) {
        if (down) {
            if (s_hotkey_held)
                return true;               /* auto-repeat of the swallowed press */
            /* Ctrl required; Alt/Win must be up (Ctrl+Alt is AltGr on many layouts). */
            if (key_down(VK_CONTROL) && !key_down(VK_MENU) &&
                !key_down(VK_LWIN)   && !key_down(VK_RWIN)) {
                s_hotkey_held = true;
                set_active(!s_active);
                return true;
            }
        } else if (s_hotkey_held) {
            s_hotkey_held = false;
            return true;
        }
        return false;                      /* plain ` / ^ keeps working normally */
    }

    /* Inactive: only finish releases of keys whose press we ate. */
    if (!s_active && down)
        return false;

    /* --- mouse mode ------------------------------------------------------ */
    switch (k->vkCode) {
    case VK_LEFT:   return on_arrow(DIR_LEFT,  down);
    case VK_RIGHT:  return on_arrow(DIR_RIGHT, down);
    case VK_UP:     return on_arrow(DIR_UP,    down);
    case VK_DOWN:   return on_arrow(DIR_DOWN,  down);
    case VK_RETURN: return on_enter(down);     /* main Enter and numpad Enter */
    default:        return false;
    }
}

static LRESULT CALLBACK ll_proc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION) {
        s_last_call = GetTickCount();
        if (handle_key((const KBDLLHOOKSTRUCT *)lp))
            return 1;                      /* swallowed: no other app sees it */
    }
    return CallNextHookEx(NULL, code, wp, lp);
}

static DWORD WINAPI hook_thread(LPVOID ready)
{
    MSG m;
    PeekMessageW(&m, NULL, WM_USER, WM_USER, PM_NOREMOVE);    /* create the queue */
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    /* NOTE: a debugger breakpoint in this thread freezes keyboard input
     * system-wide while the hook is installed. Debug with that in mind. */
    bool ok = install();
    InterlockedExchange(&s_installed, ok);
    SetEvent((HANDLE)ready);
    if (!ok)
        return 1;

    UINT_PTR wd = SetTimer(NULL, 0, WATCHDOG_MS, NULL);

    /* Hook callbacks are delivered from inside GetMessageW. */
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        switch (m.message) {
        case HM_SET_ACTIVE:
            set_active(m.wParam == 2 ? !s_active : m.wParam != 0);
            break;
        case HM_REINSTALL:
            install();
            break;
        case WM_TIMER:
            watchdog();
            break;
        }
    }

    KillTimer(NULL, wd);
    UnhookWindowsHookEx(s_hook);
    s_hook = NULL;
    mouse_release_all();
    s_active = s_hotkey_held = s_enter_held = false;
    for (int d = 0; d < DIR_COUNT; d++)
        s_arrow_held[d] = false;
    InterlockedExchange(&s_active_pub, 0);
    return 0;
}

bool hook_install(HWND notify)
{
    s_notify = notify;
    HANDLE ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ready)
        return false;
    s_thread = CreateThread(NULL, 0, hook_thread, ready, 0, &s_tid);
    if (s_thread)
        WaitForSingleObject(ready, 5000);
    CloseHandle(ready);
    return s_thread && s_installed;
}

void hook_uninstall(void)
{
    if (!s_thread)
        return;
    PostThreadMessageW(s_tid, WM_QUIT, 0, 0);
    WaitForSingleObject(s_thread, 2000);
    CloseHandle(s_thread);
    s_thread = NULL;
}

bool hook_is_active(void)
{
    return s_active_pub != 0;
}

void hook_set_active(bool on)
{
    PostThreadMessageW(s_tid, HM_SET_ACTIVE, on ? 1 : 0, 0);
}

void hook_toggle(void)
{
    PostThreadMessageW(s_tid, HM_SET_ACTIVE, 2, 0);
}
