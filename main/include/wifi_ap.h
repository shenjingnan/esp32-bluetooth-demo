/**
 * @file wifi_ap.h
 * @brief WiFi AP 模块接口
 */

#ifndef WIFI_AP_H
#define WIFI_AP_H

#include "esp_err.h"

/**
 * @brief 初始化 WiFi AP 模块
 *
 * 创建 WiFi 热点，SSID 格式为 ESP32_BLE_xxxxxx
 * 后缀为 MAC 地址后 3 字节
 *
 * @return ESP_OK 成功
 * @return 其他值 失败
 */
esp_err_t wifi_ap_init(void);

/**
 * @brief 获取 WiFi AP 的 SSID
 *
 * @return SSID 字符串指针
 */
const char* wifi_ap_get_ssid(void);

/**
 * @brief 获取 WiFi AP 的 IP 地址
 *
 * @return IP 地址字符串
 */
const char* wifi_ap_get_ip(void);

#endif /* WIFI_AP_H */