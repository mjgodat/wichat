/* fn_menu.h -- Function menu popup for WiChat */
#ifndef WICHAT_FN_MENU_H
#define WICHAT_FN_MENU_H

#ifdef __cplusplus
extern "C" {
#endif

/* Show the function menu popup. Blocks until an option is selected or dismissed.
 * Handles its own touch input and display. Calls display_flush() internally. */
void fn_menu_show(void);

#ifdef __cplusplus
}
#endif

#endif
