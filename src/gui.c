/* gui.c -- WiChat nuklear GDI GUI implementation */
#include "gui.h"

#ifdef _WIN32
/* ---- nuklear config (MUST come before includes) ---- */
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#include <nuklear.h>

#define NK_GDI_IMPLEMENTATION
#include <nuklear_gdi.h>

#include <stdio.h>
#include <string.h>

/* ---- Window procedure forwards events to nuklear ---- */
static LRESULT CALLBACK wichat_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    if (nk_gdi_handle_event(hwnd, msg, wp, lp))
        return 0;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ---- gui_init ---- */
int gui_init(gui_ctx_t *ctx, mesh_t *mesh)
{
    WNDCLASSW wc;
    HWND hwnd;
    HDC hdc;
    GdiFont *font;
    struct nk_context *nk;

    memset(ctx, 0, sizeof(*ctx));
    ctx->mesh = mesh;
    ctx->running = 1;
    ctx->fec_enabled = 1;

    /* Register window class */
    memset(&wc, 0, sizeof(wc));
    wc.style         = CS_DBLCLKS;
    wc.lpfnWndProc   = wichat_wndproc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"WiChatWindow";
    RegisterClassW(&wc);

    /* Create window */
    hwnd = CreateWindowW(L"WiChatWindow", L"WiChat",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        WICHAT_WINDOW_W, WICHAT_WINDOW_H,
        NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) return -1;

    hdc = GetDC(hwnd);
    if (!hdc) return -1;

    /* Create GDI font */
    font = nk_gdifont_create("Consolas", 16);
    if (!font) return -1;

    /* Init nuklear */
    nk = nk_gdi_init(font, hdc, WICHAT_WINDOW_W, WICHAT_WINDOW_H);
    if (!nk) return -1;

    /* Store handles */
    ctx->hwnd = (void *)hwnd;
    ctx->hdc  = (void *)hdc;
    ctx->font = (void *)font;
    ctx->nk   = (void *)nk;

    return 0;
}

/* ---- helper: send chat from input buffer ---- */
static void gui_send_chat(gui_ctx_t *ctx)
{
    if (ctx->input_buf[0] == '\0') return;
    mesh_send_chat(ctx->mesh, ctx->input_buf);
    ctx->input_buf[0] = '\0';
    ctx->input_len = 0;
    ctx->scroll_to_bottom = 1;
}

/* ---- gui_frame: one iteration of message pump + UI ---- */
int gui_frame(gui_ctx_t *ctx)
{
    struct nk_context *nk = (struct nk_context *)ctx->nk;
    HWND hwnd = (HWND)ctx->hwnd;
    MSG msg;
    mesh_t *m = ctx->mesh;
    RECT client;
    int w, h;

    /* --- Windows message pump --- */
    nk_input_begin(nk);
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            ctx->running = 0;
            nk_input_end(nk);
            return -1;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    nk_input_end(nk);

    /* Get current client area size */
    GetClientRect(hwnd, &client);
    w = client.right - client.left;
    h = client.bottom - client.top;
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    /* --- Build UI --- */
    if (nk_begin(nk, "WiChat",
        nk_rect(0, 0, (float)w, (float)h),
        NK_WINDOW_NO_SCROLLBAR))
    {
        char status_buf[128];
        int i, idx;
        nk_flags edit_flags;

        /* ---- Status bar row ---- */
        nk_layout_row_template_begin(nk, 25);
        nk_layout_row_template_push_static(nk, 80);   /* CH label */
        nk_layout_row_template_push_static(nk, 80);   /* Nodes */
        nk_layout_row_template_push_dynamic(nk);       /* callsign stretches */
        nk_layout_row_template_push_static(nk, 60);   /* Settings button */
        nk_layout_row_template_end(nk);

        snprintf(status_buf, sizeof(status_buf), "CH: %d", m->channel);
        nk_label(nk, status_buf, NK_TEXT_LEFT);

        snprintf(status_buf, sizeof(status_buf), "Nodes: %d", m->node_count);
        nk_label(nk, status_buf, NK_TEXT_LEFT);

        nk_label(nk, m->callsign, NK_TEXT_CENTERED);

        if (nk_button_label(nk, "Setup"))
            ctx->show_settings = !ctx->show_settings;

        /* ---- Horizontal rule ---- */
        nk_layout_row_dynamic(nk, 4, 1);
        nk_rule_horizontal(nk, nk_rgb(60, 60, 60), 0);

        /* ---- Chat area ---- */
        {
            float chat_h = (float)h - 110;  /* leave room for status + input + rules */
            if (chat_h < 40) chat_h = 40;
            nk_layout_row_dynamic(nk, chat_h, 1);

            if (nk_group_begin(nk, "chat", NK_WINDOW_BORDER)) {
                for (i = 0; i < m->msg_count; i++) {
                    idx = (m->msg_head - m->msg_count + i + WICHAT_MAX_MESSAGES) % WICHAT_MAX_MESSAGES;
                    snprintf(status_buf, sizeof(status_buf), "[%s] %s",
                        m->messages[idx].sender,
                        m->messages[idx].text);
                    nk_layout_row_dynamic(nk, 18, 1);
                    nk_label(nk, status_buf, NK_TEXT_LEFT);
                }

                /* Auto-scroll to bottom */
                if (ctx->scroll_to_bottom) {
                    nk_group_set_scroll(nk, "chat", 0, (nk_uint)0xFFFFFFFF);
                    ctx->scroll_to_bottom = 0;
                }

                nk_group_end(nk);
            }
        }

        /* ---- Horizontal rule ---- */
        nk_layout_row_dynamic(nk, 4, 1);
        nk_rule_horizontal(nk, nk_rgb(60, 60, 60), 0);

        /* ---- Input bar ---- */
        nk_layout_row_template_begin(nk, 30);
        nk_layout_row_template_push_dynamic(nk);       /* text input stretches */
        nk_layout_row_template_push_static(nk, 50);    /* TX button */
        nk_layout_row_template_push_static(nk, 50);    /* PING button */
        nk_layout_row_template_end(nk);

        edit_flags = nk_edit_string_zero_terminated(nk,
            NK_EDIT_FIELD | NK_EDIT_SIG_ENTER,
            ctx->input_buf, WICHAT_MAX_PAYLOAD, nk_filter_default);
        if (edit_flags & NK_EDIT_COMMITED)
            gui_send_chat(ctx);

        if (nk_button_label(nk, "TX"))
            gui_send_chat(ctx);

        if (nk_button_label(nk, "PING"))
            mesh_send_ping(ctx->mesh);

        /* ---- Settings popup ---- */
        if (ctx->show_settings) {
            struct nk_rect popup_bounds = nk_rect(
                (float)(w / 2 - 150), (float)(h / 2 - 120),
                300, 240);

            if (nk_popup_begin(nk, NK_POPUP_STATIC, "Settings",
                    NK_WINDOW_TITLE | NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR,
                    popup_bounds))
            {
                int ch;

                nk_layout_row_dynamic(nk, 30, 2);
                nk_label(nk, "Callsign:", NK_TEXT_LEFT);
                nk_edit_string_zero_terminated(nk, NK_EDIT_FIELD,
                    m->callsign, WICHAT_MAX_NAME_LEN, nk_filter_ascii);

                nk_layout_row_dynamic(nk, 30, 2);
                nk_label(nk, "Channel:", NK_TEXT_LEFT);
                ch = (int)m->channel;
                nk_property_int(nk, "#CH", 1, &ch, 14, 1, 1.0f);
                m->channel = (uint8_t)ch;

                nk_layout_row_dynamic(nk, 30, 1);
                {
                    nk_bool fec = (nk_bool)ctx->fec_enabled;
                    nk_checkbox_label(nk, "FEC Enabled", &fec);
                    ctx->fec_enabled = (int)fec;
                }

                nk_layout_row_dynamic(nk, 10, 1);
                nk_spacing(nk, 1);

                nk_layout_row_dynamic(nk, 30, 1);
                if (nk_button_label(nk, "Close")) {
                    ctx->show_settings = 0;
                    nk_popup_close(nk);
                }

                nk_popup_end(nk);
            } else {
                /* Popup was closed by clicking outside */
                ctx->show_settings = 0;
            }
        }
    }
    nk_end(nk);

    /* --- Render --- */
    nk_gdi_render(nk_rgb(20, 20, 20));
    return 0;
}

/* ---- gui_shutdown ---- */
void gui_shutdown(gui_ctx_t *ctx)
{
    nk_gdi_shutdown();
    if (ctx->font) nk_gdifont_del((GdiFont *)ctx->font);
    if (ctx->hdc && ctx->hwnd) ReleaseDC((HWND)ctx->hwnd, (HDC)ctx->hdc);
    if (ctx->hwnd) DestroyWindow((HWND)ctx->hwnd);
    UnregisterClassW(L"WiChatWindow", GetModuleHandleW(NULL));
    memset(ctx, 0, sizeof(*ctx));
}

#else
/* ---- Linux/other stub: no GUI ---- */
int  gui_init(gui_ctx_t *ctx, mesh_t *mesh)  { (void)ctx; (void)mesh; return -1; }
int  gui_frame(gui_ctx_t *ctx)               { (void)ctx; return -1; }
void gui_shutdown(gui_ctx_t *ctx)            { (void)ctx; }
#endif
