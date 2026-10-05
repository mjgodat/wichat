/* main.c -- WiChat entry point */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "config.h"
#include "frame.h"
#include "radio.h"
#include "radio_rtl8811au.h"
#include "mesh.h"
#include "gui.h"

static uint32_t get_ms(void)
{
#ifdef _WIN32
    return (uint32_t)GetTickCount();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}

int main(int argc, char **argv)
{
    const char *callsign = WICHAT_DEFAULT_NAME;
    radio_t radio;
    mesh_t mesh;
    gui_ctx_t gui;
    int radio_online = 0;
    int i;

    printf("WiChat v%s\n", WICHAT_VERSION);

    /* Parse --name argument */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--name") == 0 && i + 1 < argc) {
            callsign = argv[++i];
        }
    }

    /* ---- Create radio (fall back to offline mode) ---- */
    memset(&radio, 0, sizeof(radio));
    if (rtl8811au_create(&radio) == 0) {
        if (radio.init(&radio) == 0) {
            radio_online = 1;
            printf("Radio online.\n");
        } else {
            printf("Radio init failed -- offline mode.\n");
        }
    } else {
        printf("Radio create failed -- offline mode.\n");
    }

    /* ---- Init mesh ---- */
    mesh_init(&mesh, radio_online ? &radio : NULL, callsign);

    /* Copy MAC from radio if available, else generate random */
    if (radio_online) {
        rtl8811au_priv_t *priv = (rtl8811au_priv_t *)radio.priv;
        memcpy(mesh.local_mac, priv->local_mac, 6);
    } else {
        /* Random MAC for offline mode */
        uint32_t seed = get_ms();
        mesh.local_mac[0] = 0x02; /* locally administered */
        mesh.local_mac[1] = (uint8_t)(seed >> 0);
        mesh.local_mac[2] = (uint8_t)(seed >> 8);
        mesh.local_mac[3] = (uint8_t)(seed >> 16);
        mesh.local_mac[4] = (uint8_t)(seed >> 24);
        mesh.local_mac[5] = (uint8_t)(seed * 7);
    }

    printf("MAC: %02x:%02x:%02x:%02x:%02x:%02x  Callsign: %s\n",
        mesh.local_mac[0], mesh.local_mac[1], mesh.local_mac[2],
        mesh.local_mac[3], mesh.local_mac[4], mesh.local_mac[5],
        mesh.callsign);

    /* ---- Init GUI ---- */
    if (gui_init(&gui, &mesh) != 0) {
        fprintf(stderr, "GUI init failed.\n");
        if (radio_online) radio.shutdown(&radio);
        return 1;
    }

    /* ---- Main loop ---- */
    while (gui.running) {
        uint32_t now_ms = get_ms();

        /* Poll radio for incoming frame */
        if (radio_online) {
            uint8_t rx_buf[512];
            int rx_len = radio.rx_frame(&radio, rx_buf, sizeof(rx_buf), 1);
            if (rx_len > 0) {
                mesh_process_rx(&mesh, rx_buf, (size_t)rx_len, 0);
                gui.scroll_to_bottom = 1;
            }
        }

        /* Mesh housekeeping */
        mesh_tick(&mesh, now_ms);

        /* Render one GUI frame */
        if (gui_frame(&gui) < 0)
            break;

        /* Yield CPU */
        Sleep(1);
    }

    /* ---- Cleanup ---- */
    gui_shutdown(&gui);
    if (radio_online) radio.shutdown(&radio);

    printf("WiChat shutdown.\n");
    return 0;
}
