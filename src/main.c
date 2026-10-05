/* main.c — WiChat entry point */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#include "frame.h"
#include "radio.h"
#include "radio_rtl8811au.h"

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    printf("WiChat v%s\n", WICHAT_VERSION);
    printf("Channel: %d\n", WICHAT_DEFAULT_CH);

    /* ---- Wire format self-test ---- */
    {
        uint8_t fake_mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
        const char *msg = "Hello mesh!";

        /* Build a chat frame */
        wichat_frame_t tx;
        memset(&tx, 0, sizeof(tx));
        tx.hdr.magic  = WICHAT_MAGIC;
        tx.hdr.msg_id = frame_gen_msg_id();
        tx.hdr.ttl    = WICHAT_DEFAULT_TTL;
        tx.hdr.type   = WICHAT_TYPE_CHAT;
        memcpy(tx.hdr.sender, fake_mac, 6);
        tx.payload_len = strlen(msg);
        memcpy(tx.payload, msg, tx.payload_len);

        /* Encode */
        uint8_t wire[512];
        int enc_len = frame_encode(&tx, fake_mac, wire, sizeof(wire));
        printf("Encoded frame: %d bytes\n", enc_len);

        /* Decode */
        wichat_frame_t rx;
        memset(&rx, 0, sizeof(rx));
        int rc = frame_decode(wire, (size_t)enc_len, &rx);
        printf("Decode: %s\n", rc == 0 ? "OK" : "FAIL");
        printf("Payload: %s\n", (const char *)rx.payload);
        printf("TTL: %d, Type: 0x%02x\n", rx.hdr.ttl, rx.hdr.type);
    }

    /* ---- RTL8811AU radio test ---- */
    printf("\n--- RTL8811AU USB Radio ---\n");
    {
        radio_t radio;
        int rc = rtl8811au_create(&radio);
        if (rc != 0) {
            fprintf(stderr, "Failed to create radio object\n");
            return 1;
        }

        rc = radio.init(&radio);
        if (rc != 0) {
            printf("Radio init failed -- running in offline mode\n");
        } else {
            rtl8811au_priv_t *priv = (rtl8811au_priv_t *)radio.priv;
            printf("Radio online! MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
                   priv->local_mac[0], priv->local_mac[1], priv->local_mac[2],
                   priv->local_mac[3], priv->local_mac[4], priv->local_mac[5]);

            printf("Press Enter to shutdown...\n");
            getchar();

            radio.shutdown(&radio);
        }
    }

    return 0;
}
