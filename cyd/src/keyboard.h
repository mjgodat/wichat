/* keyboard.h -- Touch QWERTY keyboard for CYD */
#ifndef WICHAT_KEYBOARD_H
#define WICHAT_KEYBOARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Keyboard event types */
typedef enum {
    KB_NONE = 0,
    KB_CHAR,
    KB_BACKSPACE,
    KB_ENTER,
    KB_SHIFT,
    KB_FUNC
} kb_event_type_t;

/* Keyboard event */
typedef struct {
    kb_event_type_t type;
    char            ch;    /* valid when type == KB_CHAR */
} kb_event_t;

/* Initialize keyboard state. */
void keyboard_init(void);

/* Draw the keyboard at the bottom of the screen. */
void keyboard_draw(void);

/* Poll for a touch event. Returns the event; type=KB_NONE if no touch.
 * Uses LovyanGFX tft.getTouch() internally. 200ms debounce. */
kb_event_t keyboard_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* WICHAT_KEYBOARD_H */
