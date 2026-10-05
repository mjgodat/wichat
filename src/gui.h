/* gui.h -- WiChat nuklear GDI GUI */
#ifndef WICHAT_GUI_H
#define WICHAT_GUI_H

#include "mesh.h"
#include "config.h"

typedef struct {
    mesh_t *mesh;
    char    input_buf[WICHAT_MAX_PAYLOAD];
    int     input_len;
    int     show_settings;
    int     scroll_to_bottom;
    char    passphrase[64];
    int     fec_enabled;
    int     running;

    /* internal handles (opaque to callers) */
#ifdef _WIN32
    void   *hwnd;       /* HWND */
    void   *hdc;        /* HDC  */
    void   *font;       /* GdiFont* */
    void   *nk;         /* struct nk_context* */
#endif
} gui_ctx_t;

int  gui_init(gui_ctx_t *ctx, mesh_t *mesh);
int  gui_frame(gui_ctx_t *ctx);
void gui_shutdown(gui_ctx_t *ctx);

#endif /* WICHAT_GUI_H */
