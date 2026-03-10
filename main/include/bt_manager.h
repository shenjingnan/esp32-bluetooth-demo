/**
 * @file bt_manager.h
 * @brief 蓝牙管理器模块接口
 */

#ifndef BT_MANAGER_H
#define BT_MANAGER_H

#include "esp_err.h"
#include "app_common.h"

/**
 * @brief 初始化蓝牙管理器
 *
 * @return ESP_OK 成功
 * @return 其他值 失败
 */
esp_err_t bt_manager_init(void);

/**
 * @brief 开始蓝牙设备扫描
 *
 * @return ESP_OK 成功
 * @return 其他值 失败
 */
esp_err_t bt_manager_start_scan(void);

/**
 * @brief 停止蓝牙设备扫描
 *
 * @return ESP_OK 成功
 * @return 其他值 失败
 */
esp_err_t bt_manager_stop_scan(void);

/**
 * @brief 获取扫描状态
 *
 * @return bt_scan_state_t 扫描状态
 */
bt_scan_state_t bt_manager_get_scan_state(void);

/**
 * @brief 获取发现的设备列表
 *
 * @param devices 设备数组指针
 * @param count 设备数量指针
 * @return ESP_OK 成功
 */
esp_err_t bt_manager_get_devices(bt_device_info_t *devices, uint8_t *count);

/**
 * @brief 配对蓝牙设备
 *
 * @param bda 蓝牙设备地址
 * @return ESP_OK 成功
 * @return 其他值 失败
 */
esp_err_t bt_manager_pair_device(esp_bd_addr_t bda);

/**
 * @brief 取消配对蓝牙设备
 *
 * @param bda 蓝牙设备地址
 * @return ESP_OK 成功
 * @return 其他值 失败
 */
esp_err_t bt_manager_unpair_device(esp_bd_addr_t bda);

/**
 * @brief 获取已配对设备列表
 *
 * @param devices 设备数组指针
 * @param count 设备数量指针
 * @return ESP_OK 成功
 */
esp_err_t bt_manager_get_paired_devices(bt_device_info_t *devices, uint8_t *count);

#endif /* BT_MANAGER_H */