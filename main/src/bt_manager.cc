/**
 * @file bt_manager.c
 * @brief 蓝牙管理器模块实现
 */

#include <string.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "bt_manager.h"
#include "http_server.h"
#include "app_common.h"

static const char *TAG = "BT_MGR";

static bt_manager_state_t g_bt_state = {0};
static SemaphoreHandle_t g_bt_mutex = NULL;

/* 蓝牙地址转字符串 */
static char* bda2str(esp_bd_addr_t bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }
    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}

/* 从 EIR 数据中获取设备名称 */
static bool get_name_from_eir(uint8_t *eir, uint8_t *bdname, uint8_t *bdname_len)
{
    if (!eir) {
        return false;
    }

    uint8_t *rmt_bdname = NULL;
    uint8_t rmt_bdname_len = 0;

    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname) {
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if (rmt_bdname && rmt_bdname_len > 0) {
        if (rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN) {
            rmt_bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        }
        if (bdname) {
            memcpy(bdname, rmt_bdname, rmt_bdname_len);
            bdname[rmt_bdname_len] = '\0';
        }
        if (bdname_len) {
            *bdname_len = rmt_bdname_len;
        }
        return true;
    }

    return false;
}

/* 查找或添加设备到列表 */
static bt_device_info_t* find_or_add_device(esp_bd_addr_t bda)
{
    // 先查找是否已存在
    for (int i = 0; i < g_bt_state.device_count; i++) {
        if (memcmp(g_bt_state.devices[i].bda, bda, ESP_BD_ADDR_LEN) == 0) {
            return &g_bt_state.devices[i];
        }
    }

    // 添加新设备
    if (g_bt_state.device_count < MAX_DEVICES) {
        bt_device_info_t *dev = &g_bt_state.devices[g_bt_state.device_count];
        memset(dev, 0, sizeof(bt_device_info_t));
        memcpy(dev->bda, bda, ESP_BD_ADDR_LEN);
        strcpy(dev->name, "Unknown");
        dev->discovered = true;
        g_bt_state.device_count++;
        return dev;
    }

    return NULL;
}

/* 更新设备信息 */
static void update_device_info(esp_bt_gap_cb_param_t *param)
{
    char bda_str[18];
    uint32_t cod = 0;
    int32_t rssi = -129;
    uint8_t *bdname = NULL;
    uint8_t bdname_len = 0;
    uint8_t *eir = NULL;
    uint8_t eir_len = 0;
    esp_bt_gap_dev_prop_t *p;

    ESP_LOGI(TAG, "Device found: %s", bda2str(param->disc_res.bda, bda_str, 18));

    // 解析设备属性
    for (int i = 0; i < param->disc_res.num_prop; i++) {
        p = param->disc_res.prop + i;
        switch (p->type) {
        case ESP_BT_GAP_DEV_PROP_COD:
            cod = *(uint32_t *)(p->val);
            ESP_LOGD(TAG, "  CoD: 0x%"PRIx32, cod);
            break;
        case ESP_BT_GAP_DEV_PROP_RSSI:
            rssi = *(int8_t *)(p->val);
            ESP_LOGD(TAG, "  RSSI: %"PRId32, rssi);
            break;
        case ESP_BT_GAP_DEV_PROP_BDNAME:
            bdname_len = (p->len > ESP_BT_GAP_MAX_BDNAME_LEN) ? ESP_BT_GAP_MAX_BDNAME_LEN : (uint8_t)p->len;
            bdname = (uint8_t *)(p->val);
            break;
        case ESP_BT_GAP_DEV_PROP_EIR:
            eir_len = p->len;
            eir = (uint8_t *)(p->val);
            break;
        default:
            break;
        }
    }

    // 过滤音频设备
    if (!esp_bt_gap_is_valid_cod(cod)) {
        return;
    }

    // 只保留音频/视频设备
    uint8_t major_dev = esp_bt_gap_get_cod_major_dev(cod);
    if (major_dev != ESP_BT_COD_MAJOR_DEV_AV &&
        major_dev != ESP_BT_COD_MAJOR_DEV_PHONE) {
        // 也接受其他设备类型
    }

    // 更新设备信息
    if (xSemaphoreTake(g_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        bt_device_info_t *dev = find_or_add_device(param->disc_res.bda);
        if (dev) {
            dev->cod = cod;
            dev->rssi = (int8_t)rssi;

            // 设置设备名称
            if (bdname_len > 0 && bdname) {
                memcpy(dev->name, bdname, bdname_len);
                dev->name[bdname_len] = '\0';
                dev->name_len = bdname_len;
            } else if (eir && eir_len > 0) {
                get_name_from_eir(eir, (uint8_t*)dev->name, &dev->name_len);
            }

            ESP_LOGI(TAG, "Device: %s [%s], CoD: 0x%06"PRIx32", RSSI: %d",
                     dev->name, bda_str, cod, rssi);

            // 通知前端
            http_server_send_event(EVENT_DEVICE_FOUND, dev, NULL);
        }
        xSemaphoreGive(g_bt_mutex);
    }
}

/* 蓝牙 GAP 回调函数 */
static void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    char bda_str[18] = {0};

    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT:
        update_device_info(param);
        break;

    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            ESP_LOGI(TAG, "Device discovery stopped");
            g_bt_state.scan_state = BT_SCAN_COMPLETE;
            http_server_send_event(EVENT_SCAN_COMPLETE, NULL, "Scan completed");
        } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
            ESP_LOGI(TAG, "Device discovery started");
            g_bt_state.scan_state = BT_SCAN_RUNNING;
        }
        break;

    case ESP_BT_GAP_AUTH_CMPL_EVT:
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "Authentication success: %s [%s]",
                     param->auth_cmpl.device_name,
                     bda2str(param->auth_cmpl.bda, bda_str, sizeof(bda_str)));

            // 更新设备配对状态
            if (xSemaphoreTake(g_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                for (int i = 0; i < g_bt_state.device_count; i++) {
                    if (memcmp(g_bt_state.devices[i].bda, param->auth_cmpl.bda, ESP_BD_ADDR_LEN) == 0) {
                        g_bt_state.devices[i].paired = true;
                        http_server_send_event(EVENT_PAIR_SUCCESS, &g_bt_state.devices[i], NULL);
                        break;
                    }
                }
                xSemaphoreGive(g_bt_mutex);
            }
        } else {
            ESP_LOGE(TAG, "Authentication failed, status: %d", param->auth_cmpl.stat);
            http_server_send_event(EVENT_PAIR_FAILED, NULL, "Pairing failed");
        }
        break;

    case ESP_BT_GAP_PIN_REQ_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit: %d", param->pin_req.min_16_digit);
        if (param->pin_req.min_16_digit) {
            ESP_LOGI(TAG, "Input pin code: 0000 0000 0000 0000");
            esp_bt_pin_code_t pin_code = {0};
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
        } else {
            ESP_LOGI(TAG, "Input pin code: 0000");
            esp_bt_pin_code_t pin_code;
            pin_code[0] = '0';
            pin_code[1] = '0';
            pin_code[2] = '0';
            pin_code[3] = '0';
            esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
        }
        break;

    case ESP_BT_GAP_CFM_REQ_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_CFM_REQ_EVT compare value: %06"PRIu32, param->cfm_req.num_val);
        // 自动确认 SSP 配对
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;

    case ESP_BT_GAP_KEY_NOTIF_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey: %06"PRIu32, param->key_notif.passkey);
        break;

    case ESP_BT_GAP_KEY_REQ_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_KEY_REQ_EVT");
        break;

    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(TAG, "ESP_BT_GAP_MODE_CHG_EVT mode: %d", param->mode_chg.mode);
        break;

    default:
        ESP_LOGD(TAG, "Unhandled GAP event: %d", event);
        break;
    }
}

esp_err_t bt_manager_init(void)
{
    esp_err_t ret;

    // 创建互斥锁
    g_bt_mutex = xSemaphoreCreateMutex();
    if (!g_bt_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }

    // 初始化状态
    memset(&g_bt_state, 0, sizeof(g_bt_state));
    g_bt_state.scan_state = BT_SCAN_IDLE;

    // 释放 BLE 内存
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    // 初始化蓝牙控制器
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Enable controller failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初始化 Bluedroid 协议栈
    esp_bluedroid_config_t bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bluedroid_cfg.ssp_en = true;  // 启用 SSP

    ret = esp_bluedroid_init_with_cfg(&bluedroid_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Initialize bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Enable bluedroid failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 注册 GAP 回调
    ret = esp_bt_gap_register_callback(bt_gap_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GAP register failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 设置设备名称
    esp_bt_gap_set_device_name("ESP32_BLE_PAIR");

    // 设置可发现和可连接模式
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    // 设置 SSP IO 能力为 None（自动确认）
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    // 设置 PIN 类型
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

    g_bt_state.initialized = true;

    char bda_str[18] = {0};
    ESP_LOGI(TAG, "Bluetooth initialized. Address: %s",
             bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str, sizeof(bda_str)));

    return ESP_OK;
}

esp_err_t bt_manager_start_scan(void)
{
    if (!g_bt_state.initialized) {
        ESP_LOGE(TAG, "BT manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (g_bt_state.scan_state == BT_SCAN_RUNNING) {
        ESP_LOGW(TAG, "Scan already running");
        return ESP_OK;
    }

    // 清空设备列表
    if (xSemaphoreTake(g_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        g_bt_state.device_count = 0;
        memset(g_bt_state.devices, 0, sizeof(g_bt_state.devices));
        xSemaphoreGive(g_bt_mutex);
    }

    g_bt_state.scan_state = BT_SCAN_RUNNING;

    // 开始设备发现（10 秒）
    esp_err_t ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start discovery failed: %s", esp_err_to_name(ret));
        g_bt_state.scan_state = BT_SCAN_IDLE;
        return ret;
    }

    ESP_LOGI(TAG, "Bluetooth scan started");
    return ESP_OK;
}

esp_err_t bt_manager_stop_scan(void)
{
    if (!g_bt_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_bt_state.scan_state != BT_SCAN_RUNNING) {
        return ESP_OK;
    }

    esp_err_t ret = esp_bt_gap_cancel_discovery();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Stop discovery failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Bluetooth scan stopped");
    return ESP_OK;
}

bt_scan_state_t bt_manager_get_scan_state(void)
{
    return g_bt_state.scan_state;
}

esp_err_t bt_manager_get_devices(bt_device_info_t *devices, uint8_t *count)
{
    if (!devices || !count) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(g_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        *count = g_bt_state.device_count;
        memcpy(devices, g_bt_state.devices, sizeof(bt_device_info_t) * g_bt_state.device_count);
        xSemaphoreGive(g_bt_mutex);
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}

esp_err_t bt_manager_pair_device(esp_bd_addr_t bda)
{
    if (!g_bt_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    char bda_str[18];
    ESP_LOGI(TAG, "Pairing with device: %s", bda2str(bda, bda_str, sizeof(bda_str)));

    // 发起配对
    esp_err_t ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 1, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Pair failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

esp_err_t bt_manager_unpair_device(esp_bd_addr_t bda)
{
    if (!g_bt_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    char bda_str[18];
    ESP_LOGI(TAG, "Unpairing device: %s", bda2str(bda, bda_str, sizeof(bda_str)));

    // 移除配对设备
    esp_err_t ret = esp_bt_gap_remove_bond_device(bda);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unpair failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 更新设备状态
    if (xSemaphoreTake(g_bt_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < g_bt_state.device_count; i++) {
            if (memcmp(g_bt_state.devices[i].bda, bda, ESP_BD_ADDR_LEN) == 0) {
                g_bt_state.devices[i].paired = false;
                http_server_send_event(EVENT_UNPAIR_SUCCESS, &g_bt_state.devices[i], NULL);
                break;
            }
        }
        xSemaphoreGive(g_bt_mutex);
    }

    return ESP_OK;
}

esp_err_t bt_manager_get_paired_devices(bt_device_info_t *devices, uint8_t *count)
{
    if (!devices || !count) {
        return ESP_ERR_INVALID_ARG;
    }

    *count = 0;

    // 获取已配对设备数量
    int paired_num = esp_bt_gap_get_bond_device_num();
    if (paired_num <= 0) {
        return ESP_OK;
    }

    // 获取已配对设备列表
    esp_bd_addr_t *paired_list = malloc(sizeof(esp_bd_addr_t) * paired_num);
    if (!paired_list) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = esp_bt_gap_get_bond_device_list(&paired_num, paired_list);
    if (ret != ESP_OK) {
        free(paired_list);
        return ret;
    }

    // 填充设备信息
    for (int i = 0; i < paired_num && *count < MAX_DEVICES; i++) {
        memcpy(devices[*count].bda, paired_list[i], ESP_BD_ADDR_LEN);
        devices[*count].paired = true;
        strcpy(devices[*count].name, "Paired Device");
        (*count)++;
    }

    free(paired_list);
    return ESP_OK;
}