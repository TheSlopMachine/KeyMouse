# KeyMouse

A small native Windows tray tool (pure C, Win32 API, no dependencies) that lets you drive the mouse pointer from the keyboard.

## Usage

| Key                         | Action                                   |
|-----------------------------|------------------------------------------|
| Ctrl + `` ` `` (key under Esc) | Toggle mouse mode on/off              |
| Arrow keys                  | Move pointer (tap = 1 px, hold = accelerate) |
| Enter                       | Left button (hold + arrows = drag)       |
| Shift + Enter               | Right button                             |

Tray icon: grey = off, green = on. Left-click toggles, right-click opens the menu
(speed Slow/Normal/Fast, start with Windows, restart as administrator, exit).

While mouse mode is on, the arrows and Enter are **swallowed** before any other
application sees them. All other keys pass through untouched.

## Build

Requires MinGW-w64 `gcc`, `windres` and GNU `make` (`mingw32-make` on some installs).

```
make build            # -> build/KeyMouse.exe
make build DEBUG=1    # unoptimised, with symbols
make clean
make build CROSS=x86_64-w64-mingw32-   # cross-compile from Linux/macOS
```

## Project layout

```
Makefile
src/
  main.c       hidden window, message loop, tray/menu glue
  hook.c       WH_KEYBOARD_LL hook + mouse-mode state machine
  mouse.c      pointer movement (acceleration) and button injection
  tray.c       notification-area icon and context menu
  settings.c   registry persistence, start-with-Windows
  util.c       elevation helpers
  *.h          module interfaces (common.h is included everywhere)
res/           icons, manifest (per-monitor DPI), version info
tools/         make_icon.py regenerates res/*.ico (stdlib only)
```

## How it works

- A global low-level keyboard hook sees keys before applications do; returning
  non-zero discards the event. A key-up is swallowed only if its key-down was.
- The activation key is matched by **scancode 0x29**, so it is the physical key
  under Esc on any layout (`` ` `` US, `^` German, ...). Change `HOTKEY_SCANCODE`
  in `common.h` to use another key. Ctrl+Alt (AltGr) does not trigger it.
- Movement uses `SetCursorPos`; buttons use `SendInput`. Enter-down/up map to
  button-down/up, so dragging works. Shift+Enter remembers it pressed the right
  button, even if Shift is released first.
- Synthetic keystrokes (`LLKHF_INJECTED`) are ignored.

## Limitations

- **Elevated windows**: Windows blocks input hooks/injection into higher-integrity
  windows. Use tray menu > *Restart as administrator* to control them.
- Does not work on the UAC secure desktop or lock screen.
- Applications read the real Shift state, so Shift+Enter right-clicks arrive with
  Shift held (e.g. Explorer shows its extended context menu).
- Ctrl+`` ` `` is used by other tools (e.g. VS Code's terminal toggle); while
  KeyMouse runs it takes precedence system-wide.
- A global keyboard hook can trigger antivirus heuristics. KeyMouse records nothing.
- Settings: `HKCU\Software\KeyMouse`.
