/**
 * @file app_common.h
 * @brief 公共定义和宏
 */

#ifndef APP_COMMON_H
#define APP_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_gap_bt_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 设备名称最大长度 */
#define BT_MAX_NAME_LEN 64

/* 设备列表最大数量 */
#define MAX_DEVICES 20

/* WiFi AP 配置 */
#define WIFI_AP_SSID_PREFIX "ESP32_BLE_"
#define WIFI_AP_PASSWORD "12345678"
#define WIFI_AP_MAX_CONN 4
#define WIFI_AP_CHANNEL 1

/* 蓝牙设备信息结构 */
typedef struct {
    esp_bd_addr_t bda;              /**< 蓝牙地址 */
    char name[BT_MAX_NAME_LEN];     /**< 设备名称 */
    uint8_t name_len;               /**< 名称长度 */
    int8_t rssi;                    /**< 信号强度 */
    uint32_t cod;                   /**< 设备类型码 */
    bool paired;                    /**< 是否已配对 */
    bool discovered;                /**< 是否已发现 */
} bt_device_info_t;

/* 蓝牙扫描状态 */
typedef enum {
    BT_SCAN_IDLE = 0,
    BT_SCAN_RUNNING,
    BT_SCAN_COMPLETE,
} bt_scan_state_t;

/* 蓝牙管理器状态 */
typedef struct {
    bt_scan_state_t scan_state;     /**< 扫描状态 */
    bt_device_info_t devices[MAX_DEVICES];  /**< 设备列表 */
    uint8_t device_count;           /**< 设备数量 */
    bool initialized;               /**< 是否已初始化 */
} bt_manager_state_t;

/* 事件类型（用于 SSE 推送） */
typedef enum {
    EVENT_DEVICE_FOUND = 1,
    EVENT_SCAN_COMPLETE,
    EVENT_PAIR_SUCCESS,
    EVENT_PAIR_FAILED,
    EVENT_UNPAIR_SUCCESS,
} event_type_t;

/* SSE 事件数据 */
typedef struct {
    event_type_t type;
    bt_device_info_t device;
    char message[64];
} sse_event_t;

#ifdef __cplusplus
}
#endif

#endif /* APP_COMMON_H */