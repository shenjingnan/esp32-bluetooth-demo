/**
 * @file wifi_ap.c
 * @brief WiFi AP 模块实现
 */

#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "lwip/inet.h"

#include "wifi_ap.h"
#include "app_common.h"

static const char *TAG = "WIFI_AP";

static char ap_ssid[32] = {0};
static char ap_ip[16] = {0};

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d",
                 MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
}

esp_err_t wifi_ap_init(void)
{
    esp_err_t ret;

    // 获取 MAC 地址并生成 SSID
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    snprintf(ap_ssid, sizeof(ap_ssid), "%s%02X%02X%02X",
             WIFI_AP_SSID_PREFIX, mac[3], mac[4], mac[5]);

    // 初始化 WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 注册事件处理
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Event handler register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 配置 AP
    wifi_config_t wifi_config = {
        .ap = {
            .ssid_len = strlen(ap_ssid),
            .password = WIFI_AP_PASSWORD,
            .max_connection = WIFI_AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .channel = WIFI_AP_CHANNEL,
        },
    };
    strcpy((char *)wifi_config.ap.ssid, ap_ssid);

    // 设置 AP 模式
    ret = esp_wifi_set_mode(WIFI_MODE_AP);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set AP mode failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set AP config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 获取 IP 地址
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (netif) {
        esp_netif_get_ip_info(netif, &ip_info);
        inet_ntoa_r(ip_info.ip.addr, ap_ip, sizeof(ap_ip));
    }

    ESP_LOGI(TAG, "WiFi AP started. SSID: %s, Password: %s, IP: %s",
             ap_ssid, WIFI_AP_PASSWORD, ap_ip);

    return ESP_OK;
}

const char* wifi_ap_get_ssid(void)
{
    return ap_ssid;
}

const char* wifi_ap_get_ip(void)
{
    return ap_ip;
}