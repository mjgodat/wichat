/* radio_esp32.h -- ESP32 promiscuous-mode radio for WiChat
 * Uses WiFi promiscuous mode to TX/RX raw 802.11 frames on the
 * WiChat BSSID (57:43:57:43:57:43). No AP or STA connection needed. */
#ifndef WICHAT_RADIO_ESP32_H
#define WICHAT_RADIO_ESP32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize ESP32 promiscuous radio on the given channel.
 * Sets up NVS, WiFi in NULL mode, promiscuous RX with data filter,
 * 1 Mbps rate, max TX power. Creates FreeRTOS queue for RX frames.
 * Returns 0 on success, -1 on error. */
int radio_esp32_init(uint8_t channel);

/* Get the ESP32 station MAC address into mac[6]. */
void radio_esp32_get_mac(uint8_t mac[6]);

/* Transmit a raw 802.11 frame.
 * Returns 0 on success, -1 on error. */
int radio_esp32_tx(const uint8_t *data, size_t len);

/* Print RX stats to Serial for debugging */
void radio_esp32_print_stats(void);

/* Non-blocking receive. Copies up to max_len bytes into buf.
 * Writes RSSI to *rssi if non-NULL.
 * Returns number of bytes received, or 0 if queue is empty. */
int radio_esp32_rx(uint8_t *buf, size_t max_len, int8_t *rssi);

#ifdef __cplusplus
}
#endif

#endif /* WICHAT_RADIO_ESP32_H */
