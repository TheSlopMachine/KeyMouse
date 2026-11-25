/*
 * Cursor movement and button injection, on a dedicated high-priority thread.
 *
 * The public functions only post a command to that thread, so callers (the
 * keyboard hook in particular) never block on SendInput / SetCursorPos.
 *
 * Movement uses SetCursorPos (exact pixels, unaffected by the "enhance pointer
 * precision" acceleration curve), driven by a high-resolution waitable timer
 * (~2 ms) instead of WM_TIMER (15.6 ms granularity, lowest-priority message,
 * coalesced for background apps). Speed is time-based, so jitter in the tick
 * interval does not change how far the pointer travels.
 *
 * Buttons use SendInput at the current cursor position, so press/release map
 * 1:1 to key down/up and dragging just works.
 */
#include "mouse.h"
#include <mmsystem.h>                       /* timeBeginPeriod (fallback only) */

typedef struct {
    const wchar_t *name;
    double start;      /* px/s when a hold begins    */
    double top;        /* px/s once fully ramped     */
} SpeedPreset;

static const SpeedPreset PRESETS[SPEED_COUNT] = {
    { L"Slow",   125.0,  400.0 },
    { L"Normal", 250.0,  800.0 },
    { L"Fast",   500.0, 1600.0 },
};

#define TICK_100NS    20000                 /* 2 ms between cursor updates       */
#define RAMP_MS       2000.0                /* smoothstep ramp from start to top */
#define DIAGONAL      0.70710678118654752   /* 1/sqrt(2): same speed on diagonals */

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

enum {
    MM_DIR = WM_APP + 20,            /* wParam: Dir,    lParam: down */
    MM_BTN,                          /* wParam: Button, lParam: down */
    MM_RELEASE_ALL
};

static HANDLE        s_thread;
static DWORD         s_tid;
static volatile LONG s_speed = 1;

/* --- mouse-thread-only state -------------------------------------------- */
static HANDLE        s_timer;
static bool          s_hires;               /* false: fall back to timeBeginPeriod */
static bool          s_dir[DIR_COUNT];
static int           s_held;                /* arrows currently down */
static bool          s_btn[BTN_COUNT];
static double        s_rem_x, s_rem_y;      /* sub-pixel remainder   */
static LARGE_INTEGER s_freq, s_start, s_last;

static void move_cursor(int dx, int dy)
{
    POINT p;
    if ((dx || dy) && GetCursorPos(&p))
        SetCursorPos(p.x + dx, p.y + dy);
}

static void arm_timer(void)
{
    LARGE_INTEGER due;
    due.QuadPart = -TICK_100NS;             /* relative */
    SetWaitableTimer(s_timer, &due, 0, NULL, NULL, FALSE);
}

static void do_dir(Dir d, bool down)
{
    static const int DX[DIR_COUNT] = { -1, 1,  0, 0 };
    static const int DY[DIR_COUNT] = {  0, 0, -1, 1 };

    if (s_dir[d] == down)
        return;                              /* keyboard auto-repeat / duplicate */
    s_dir[d] = down;

    if (down) {
        move_cursor(DX[d], DY[d]);           /* a tap moves exactly one pixel */
        if (s_held++ == 0) {
            QueryPerformanceCounter(&s_start);
            s_last = s_start;
            s_rem_x = s_rem_y = 0.0;
            if (!s_hires)
                timeBeginPeriod(1);
            arm_timer();
        }
    } else if (--s_held == 0) {
        CancelWaitableTimer(s_timer);
        if (!s_hires)
            timeEndPeriod(1);
    }
}

static void tick(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    double dt   = (double)(now.QuadPart - s_last.QuadPart) / (double)s_freq.QuadPart;
    double held = (double)(now.QuadPart - s_start.QuadPart) * 1000.0 / (double)s_freq.QuadPart;
    s_last = now;
    if (dt > 0.1)
        dt = 0.1;                            /* don't jump after a stall */

    int ix = (int)s_dir[DIR_RIGHT] - (int)s_dir[DIR_LEFT];
    int iy = (int)s_dir[DIR_DOWN]  - (int)s_dir[DIR_UP];
    if (!ix && !iy)
        return;

    const SpeedPreset *p = &PRESETS[s_speed];
    double t = held / RAMP_MS;
    if (t > 1.0)
        t = 1.0;
    t = t * t * (3.0 - 2.0 * t);             /* smoothstep: soft launch, smooth top */
    double speed = p->start + (p->top - p->start) * t;
    double k = (ix && iy) ? DIAGONAL : 1.0;

    s_rem_x += ix * speed * k * dt;
    s_rem_y += iy * speed * k * dt;

    int mx = (int)s_rem_x;                   /* truncates toward zero */
    int my = (int)s_rem_y;
    s_rem_x -= mx;
    s_rem_y -= my;
    move_cursor(mx, my);
}

static void do_button(Button b, bool down)
{
    if (s_btn[b] == down)
        return;
    s_btn[b] = down;

    INPUT in;
    ZeroMemory(&in, sizeof in);
    in.type = INPUT_MOUSE;
    if (b == BTN_LEFT)
        in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN  : MOUSEEVENTF_LEFTUP;
    else
        in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    SendInput(1, &in, sizeof in);
}

static void do_release_all(void)
{
    for (int d = 0; d < DIR_COUNT; d++)
        do_dir((Dir)d, false);
    for (int b = 0; b < BTN_COUNT; b++)
        do_button((Button)b, false);
}

static DWORD WINAPI mouse_thread(LPVOID ready)
{
    MSG m;
    PeekMessageW(&m, NULL, WM_USER, WM_USER, PM_NOREMOVE);    /* create the queue */
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    QueryPerformanceFrequency(&s_freq);

    s_timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                     TIMER_ALL_ACCESS);
    s_hires = s_timer != NULL;               /* Windows 10 1803+ */
    if (!s_timer)
        s_timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    SetEvent((HANDLE)ready);

    for (;;) {
        DWORD n = s_held ? 1 : 0;            /* wait on the timer only while moving */
        DWORD r = MsgWaitForMultipleObjectsEx(n, &s_timer, INFINITE, QS_POSTMESSAGE,
                                              MWMO_INPUTAVAILABLE);
        if (n && r == WAIT_OBJECT_0 && s_held) {
            tick();
            arm_timer();
        }

        while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
            switch (m.message) {
            case MM_DIR:
                if (m.wParam < DIR_COUNT)
                    do_dir((Dir)m.wParam, m.lParam != 0);
                break;
            case MM_BTN:
                if (m.wParam < BTN_COUNT)
                    do_button((Button)m.wParam, m.lParam != 0);
                break;
            case MM_RELEASE_ALL:
                do_release_all();
                break;
            case WM_QUIT:
                do_release_all();
                CloseHandle(s_timer);
                return 0;
            }
        }
    }
}

bool mouse_init(void)
{
    HANDLE ready = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!ready)
        return false;
    s_thread = CreateThread(NULL, 0, mouse_thread, ready, 0, &s_tid);
    if (s_thread)
        WaitForSingleObject(ready, 5000);
    CloseHandle(ready);
    return s_thread != NULL;
}

void mouse_shutdown(void)
{
    if (!s_thread)
        return;
    PostThreadMessageW(s_tid, WM_QUIT, 0, 0);   /* queued after any pending release */
    WaitForSingleObject(s_thread, 2000);
    CloseHandle(s_thread);
    s_thread = NULL;
}

void mouse_set_speed(int preset)
{
    if (preset >= 0 && preset < SPEED_COUNT)
        InterlockedExchange(&s_speed, preset);
}

const wchar_t *mouse_speed_name(int preset)
{
    return (preset >= 0 && preset < SPEED_COUNT) ? PRESETS[preset].name : L"?";
}

void mouse_dir_press(Dir d)       { PostThreadMessageW(s_tid, MM_DIR, d, 1); }
void mouse_dir_release(Dir d)     { PostThreadMessageW(s_tid, MM_DIR, d, 0); }
void mouse_button(Button b, bool down) { PostThreadMessageW(s_tid, MM_BTN, b, down); }
void mouse_release_all(void)      { PostThreadMessageW(s_tid, MM_RELEASE_ALL, 0, 0); }
