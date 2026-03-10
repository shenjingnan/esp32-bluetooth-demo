/**
 * @file main.c
 * @brief ESP32 蓝牙配对主程序
 *
 * 本程序实现以下功能:
 * 1. 创建 WiFi 热点 (SSID: ESP32_BLE_xxxxxx)
 * 2. 提供 HTTP 服务器和蓝牙配对页面
 * 3. 扫描周围蓝牙设备
 * 4. 支持蓝牙设备配对
 */

#include <stdio.h>
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi_default.h"

#include "wifi_ap.h"
#include "http_server.h"
#include "bt_manager.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32 Bluetooth Pairing Demo ===");
    ESP_LOGI(TAG, "Initializing...");

    // 1. 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized");

    // 2. 初始化网络协议栈
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_LOGI(TAG, "Netif initialized");

    // 3. 创建默认事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "Event loop created");

    // 4. 创建默认 WiFi AP netif
    esp_netif_create_default_wifi_ap();

    // 5. 初始化 WiFi AP
    ret = wifi_ap_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi AP init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "WiFi AP started. SSID: %s", wifi_ap_get_ssid());

    // 6. 初始化蓝牙管理器
    ret = bt_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "BT manager init failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "Bluetooth manager initialized");

    // 7. 启动 HTTP 服务器
    ret = http_server_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "HTTP server started");

    // 打印连接信息
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Setup Complete!");
    ESP_LOGI(TAG, "  WiFi SSID: %s", wifi_ap_get_ssid());
    ESP_LOGI(TAG, "  WiFi Password: 12345678");
    ESP_LOGI(TAG, "  Web URL: http://%s/", wifi_ap_get_ip());
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Connect to WiFi and open the URL to pair Bluetooth devices.");
}