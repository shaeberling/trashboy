#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WIFI_MGR_MAX_APS 16
#define WIFI_MGR_SSID_LEN 33   // 32 + NUL
#define WIFI_MGR_PASS_LEN 65   // 64 + NUL

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char ssid[WIFI_MGR_SSID_LEN];
    int8_t  rssi;
    uint8_t authmode;          // wifi_auth_mode_t value; opaque to the UI
} wifi_mgr_ap_t;

// Bring up netif + default event loop + Wi-Fi in STA mode. Idempotent.
void wifi_mgr_init(void);

// Synchronous active scan. Fills `aps` with up to `max` records, sorted by
// RSSI (strongest first). Returns the number written.
int wifi_mgr_scan(wifi_mgr_ap_t *aps, int max);

// Block until associated + got IP, or until timeout. Returns true on success.
bool wifi_mgr_connect(const char *ssid, const char *password, int timeout_ms);

bool wifi_mgr_is_connected(void);

// Read stored credentials from NVS. Returns false if either is missing.
bool wifi_mgr_load_creds(char *ssid, size_t ssid_len,
                         char *password, size_t pass_len);

// Persist credentials to NVS for next boot.
void wifi_mgr_save_creds(const char *ssid, const char *password);

// Erase stored credentials.
void wifi_mgr_clear_creds(void);

#ifdef __cplusplus
}
#endif
