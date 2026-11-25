#ifndef MOUSE_H
#define MOUSE_H

#include "common.h"

typedef enum { DIR_LEFT, DIR_RIGHT, DIR_UP, DIR_DOWN, DIR_COUNT } Dir;
typedef enum { BTN_LEFT, BTN_RIGHT, BTN_COUNT } Button;

#define SPEED_COUNT 3

/* Start / stop the mouse thread. Everything else below is safe to call from
 * any thread and never blocks: it just queues a command for that thread. */
bool mouse_init(void);
void mouse_shutdown(void);

void           mouse_set_speed(int preset);
const wchar_t *mouse_speed_name(int preset);

void mouse_dir_press(Dir d);      /* safe to call on key auto-repeat */
void mouse_dir_release(Dir d);

void mouse_button(Button b, bool down);

/* Stop moving and release any button we are holding down. */
void mouse_release_all(void);

#endif
