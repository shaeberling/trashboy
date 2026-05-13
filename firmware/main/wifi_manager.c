#include "wifi_manager.h"

#include <string.h>
#include <stdlib.h>

#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "wifi_mgr";

#define WIFI_BIT_CONNECTED BIT0
#define WIFI_BIT_FAILED    BIT1

#define NVS_NS        "wifi_mgr"
#define NVS_KEY_SSID  "ssid"
#define NVS_KEY_PASS  "pass"

static EventGroupHandle_t s_events = NULL;
static bool s_initialized = false;
static bool s_connect_in_progress = false;

static void event_handler(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA start");
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *) data;
            ESP_LOGW(TAG, "STA disconnect, reason=%d", d ? d->reason : -1);
            if (s_connect_in_progress) {
                xEventGroupSetBits(s_events, WIFI_BIT_FAILED);
            }
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *) data;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&e->ip_info.ip));
        xEventGroupSetBits(s_events, WIFI_BIT_CONNECTED);
    }
}

void wifi_mgr_init(void)
{
    if (s_initialized) return;
    s_initialized = true;

    s_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static int compare_rssi_desc(const void *a, const void *b)
{
    const wifi_ap_record_t *aa = (const wifi_ap_record_t *) a;
    const wifi_ap_record_t *bb = (const wifi_ap_record_t *) b;
    return (int) bb->rssi - (int) aa->rssi;
}

int wifi_mgr_scan(wifi_mgr_ap_t *aps, int max)
{
    if (max <= 0) return 0;

    wifi_scan_config_t scan_cfg = {
        .ssid = NULL, .bssid = NULL, .channel = 0,
        .show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
        ESP_LOGE(TAG, "scan start failed");
        return 0;
    }

    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num == 0) return 0;

    wifi_ap_record_t *recs = calloc(num, sizeof(wifi_ap_record_t));
    if (!recs) return 0;
    esp_wifi_scan_get_ap_records(&num, recs);

    qsort(recs, num, sizeof(wifi_ap_record_t), compare_rssi_desc);

    // De-duplicate by SSID, keeping the strongest entry per name. The list
    // is already RSSI-sorted, so the first occurrence of each SSID is the
    // one we want; subsequent ones are duplicates from other channels/BSSIDs.
    int out = 0;
    for (uint16_t i = 0; i < num && out < max; i++) {
        const char *ssid = (const char *) recs[i].ssid;
        if (ssid[0] == '\0') continue;  // hidden SSIDs, skip
        bool seen = false;
        for (int j = 0; j < out; j++) {
            if (strncmp(aps[j].ssid, ssid, WIFI_MGR_SSID_LEN - 1) == 0) {
                seen = true;
                break;
            }
        }
        if (seen) continue;
        strncpy(aps[out].ssid, ssid, WIFI_MGR_SSID_LEN - 1);
        aps[out].ssid[WIFI_MGR_SSID_LEN - 1] = '\0';
        aps[out].rssi = recs[i].rssi;
        aps[out].authmode = (uint8_t) recs[i].authmode;
        out++;
    }
    free(recs);
    return out;
}

bool wifi_mgr_connect(const char *ssid, const char *password, int timeout_ms)
{
    if (!s_events) return false;

    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy((char *) cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    if (password) {
        strncpy((char *) cfg.sta.password, password, sizeof(cfg.sta.password) - 1);
    }
    // Accept any auth that matches our password; if there's no password,
    // require open. ESP-IDF picks the strongest mode advertised by the AP
    // that matches our credentials.
    cfg.sta.threshold.authmode = (password && password[0]) ? WIFI_AUTH_WEP : WIFI_AUTH_OPEN;

    xEventGroupClearBits(s_events, WIFI_BIT_CONNECTED | WIFI_BIT_FAILED);
    s_connect_in_progress = true;

    if (esp_wifi_set_config(WIFI_IF_STA, &cfg) != ESP_OK) {
        s_connect_in_progress = false;
        return false;
    }
    esp_wifi_disconnect();
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        s_connect_in_progress = false;
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(
        s_events, WIFI_BIT_CONNECTED | WIFI_BIT_FAILED,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    s_connect_in_progress = false;

    return (bits & WIFI_BIT_CONNECTED) != 0;
}

bool wifi_mgr_is_connected(void)
{
    if (!s_events) return false;
    return (xEventGroupGetBits(s_events) & WIFI_BIT_CONNECTED) != 0;
}

bool wifi_mgr_load_creds(char *ssid, size_t ssid_len,
                         char *password, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    esp_err_t e1 = nvs_get_str(h, NVS_KEY_SSID, ssid, &ssid_len);
    esp_err_t e2 = nvs_get_str(h, NVS_KEY_PASS, password, &pass_len);
    nvs_close(h);
    return e1 == ESP_OK && e2 == ESP_OK;
}

void wifi_mgr_save_creds(const char *ssid, const char *password)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_KEY_SSID, ssid);
    nvs_set_str(h, NVS_KEY_PASS, password ? password : "");
    nvs_commit(h);
    nvs_close(h);
}

void wifi_mgr_clear_creds(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_key(h, NVS_KEY_SSID);
    nvs_erase_key(h, NVS_KEY_PASS);
    nvs_commit(h);
    nvs_close(h);
}
