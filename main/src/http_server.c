/**
 * @file http_server.c
 * @brief HTTP 服务器模块实现
 */

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "http_server.h"
#include "bt_manager.h"
#include "app_common.h"

static const char *TAG = "HTTP_SRV";

static httpd_handle_t g_server = NULL;
static SemaphoreHandle_t g_sse_mutex = NULL;

/* SSE 客户端连接链表 */
#define MAX_SSE_CLIENTS 4
static httpd_req_t* g_sse_clients[MAX_SSE_CLIENTS] = {0};
static int g_sse_client_count = 0;

/* 嵌入的 Web 文件 */
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");
extern const char style_css_start[] asm("_binary_style_css_start");
extern const char style_css_end[] asm("_binary_style_css_end");
extern const char app_js_start[] asm("_binary_app_js_start");
extern const char app_js_end[] asm("_binary_app_js_end");

/* 蓝牙地址字符串解析 */
static int str2bda(const char *str, esp_bd_addr_t bda)
{
    if (!str || !bda) return -1;

    int a, b, c, d, e, f;
    if (sscanf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
               &a, &b, &c, &d, &e, &f) != 6) {
        return -1;
    }

    bda[0] = (uint8_t)a;
    bda[1] = (uint8_t)b;
    bda[2] = (uint8_t)c;
    bda[3] = (uint8_t)d;
    bda[4] = (uint8_t)e;
    bda[5] = (uint8_t)f;

    return 0;
}

/* 蓝牙地址转字符串 */
static char* bda2str(esp_bd_addr_t bda, char *str, size_t size)
{
    if (!bda || !str || size < 18) return NULL;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    return str;
}

/* 设备信息转 JSON */
static cJSON* device_to_json(bt_device_info_t *dev)
{
    cJSON *json = cJSON_CreateObject();
    char bda_str[18];

    cJSON_AddStringToObject(json, "address", bda2str(dev->bda, bda_str, sizeof(bda_str)));
    cJSON_AddStringToObject(json, "name", dev->name);
    cJSON_AddNumberToObject(json, "rssi", dev->rssi);
    cJSON_AddNumberToObject(json, "cod", dev->cod);
    cJSON_AddBoolToObject(json, "paired", dev->paired);

    return json;
}

/* GET / - 主页 */
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

/* GET /style.css */
static esp_err_t style_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, style_css_start, style_css_end - style_css_start);
    return ESP_OK;
}

/* GET /app.js */
static esp_err_t js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, app_js_start, app_js_end - app_js_start);
    return ESP_OK;
}

/* POST /api/scan - 开始扫描 */
static esp_err_t scan_handler(httpd_req_t *req)
{
    esp_err_t ret = bt_manager_start_scan();
    cJSON *json = cJSON_CreateObject();

    if (ret == ESP_OK) {
        cJSON_AddBoolToObject(json, "success", true);
        cJSON_AddStringToObject(json, "message", "Scan started");
    } else {
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "message", "Scan failed");
    }

    const char *resp = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);

    free((void*)resp);
    cJSON_Delete(json);
    return ESP_OK;
}

/* POST /api/scan/stop - 停止扫描 */
static esp_err_t scan_stop_handler(httpd_req_t *req)
{
    esp_err_t ret = bt_manager_stop_scan();
    cJSON *json = cJSON_CreateObject();

    if (ret == ESP_OK) {
        cJSON_AddBoolToObject(json, "success", true);
        cJSON_AddStringToObject(json, "message", "Scan stopped");
    } else {
        cJSON_AddBoolToObject(json, "success", false);
        cJSON_AddStringToObject(json, "message", "Stop failed");
    }

    const char *resp = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);

    free((void*)resp);
    cJSON_Delete(json);
    return ESP_OK;
}

/* GET /api/devices - 获取设备列表 */
static esp_err_t devices_handler(httpd_req_t *req)
{
    bt_device_info_t devices[MAX_DEVICES];
    uint8_t count = 0;

    esp_err_t ret = bt_manager_get_devices(devices, &count);

    cJSON *json = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();

    if (ret == ESP_OK) {
        for (int i = 0; i < count; i++) {
            cJSON_AddItemToArray(arr, device_to_json(&devices[i]));
        }
    }

    cJSON_AddItemToObject(json, "devices", arr);
    cJSON_AddNumberToObject(json, "count", count);
    cJSON_AddStringToObject(json, "scanState",
        bt_manager_get_scan_state() == BT_SCAN_RUNNING ? "scanning" :
        bt_manager_get_scan_state() == BT_SCAN_COMPLETE ? "complete" : "idle");

    const char *resp = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);

    free((void*)resp);
    cJSON_Delete(json);
    return ESP_OK;
}

/* POST /api/pair - 发起配对 */
static esp_err_t pair_handler(httpd_req_t *req)
{
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *addr = cJSON_GetObjectItem(json, "address");
    if (!addr || !cJSON_IsString(addr)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing address");
        return ESP_FAIL;
    }

    esp_bd_addr_t bda;
    if (str2bda(addr->valuestring, bda) != 0) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid address");
        return ESP_FAIL;
    }

    esp_err_t err = bt_manager_pair_device(bda);

    cJSON *resp_json = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddBoolToObject(resp_json, "success", true);
        cJSON_AddStringToObject(resp_json, "message", "Pairing initiated");
    } else {
        cJSON_AddBoolToObject(resp_json, "success", false);
        cJSON_AddStringToObject(resp_json, "message", "Pairing failed");
    }

    const char *resp = cJSON_PrintUnformatted(resp_json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);

    free((void*)resp);
    cJSON_Delete(resp_json);
    cJSON_Delete(json);
    return ESP_OK;
}

/* POST /api/unpair - 取消配对 */
static esp_err_t unpair_handler(httpd_req_t *req)
{
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *addr = cJSON_GetObjectItem(json, "address");
    if (!addr || !cJSON_IsString(addr)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing address");
        return ESP_FAIL;
    }

    esp_bd_addr_t bda;
    if (str2bda(addr->valuestring, bda) != 0) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid address");
        return ESP_FAIL;
    }

    esp_err_t err = bt_manager_unpair_device(bda);

    cJSON *resp_json = cJSON_CreateObject();
    if (err == ESP_OK) {
        cJSON_AddBoolToObject(resp_json, "success", true);
        cJSON_AddStringToObject(resp_json, "message", "Unpaired successfully");
    } else {
        cJSON_AddBoolToObject(resp_json, "success", false);
        cJSON_AddStringToObject(resp_json, "message", "Unpair failed");
    }

    const char *resp = cJSON_PrintUnformatted(resp_json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);

    free((void*)resp);
    cJSON_Delete(resp_json);
    cJSON_Delete(json);
    return ESP_OK;
}

/* GET /api/status - 获取状态 */
static esp_err_t status_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();

    cJSON_AddStringToObject(json, "scanState",
        bt_manager_get_scan_state() == BT_SCAN_RUNNING ? "scanning" :
        bt_manager_get_scan_state() == BT_SCAN_COMPLETE ? "complete" : "idle");

    const char *resp = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);

    free((void*)resp);
    cJSON_Delete(json);
    return ESP_OK;
}

/* GET /api/events - SSE 事件流 */
static esp_err_t events_handler(httpd_req_t *req)
{
    // 设置 SSE 响应头
    httpd_resp_set_type(req, "text/event-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_set_hdr(req, "Connection", "keep-alive");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    // 添加客户端到列表
    if (xSemaphoreTake(g_sse_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (g_sse_client_count < MAX_SSE_CLIENTS) {
            g_sse_clients[g_sse_client_count++] = req;
        }
        xSemaphoreGive(g_sse_mutex);
    }

    // 发送初始连接事件
    httpd_resp_sendstr_chunk(req, "event: connected\ndata: ok\n\n");

    // 保持连接
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // 发送心跳
        if (httpd_resp_sendstr_chunk(req, ": heartbeat\n\n") != ESP_OK) {
            break;
        }
    }

    // 移除客户端
    if (xSemaphoreTake(g_sse_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        for (int i = 0; i < g_sse_client_count; i++) {
            if (g_sse_clients[i] == req) {
                // 将最后一个移到当前位置
                g_sse_clients[i] = g_sse_clients[--g_sse_client_count];
                break;
            }
        }
        xSemaphoreGive(g_sse_mutex);
    }

    return ESP_OK;
}

void http_server_send_event(int event_type, void *device, const char *message)
{
    if (!g_sse_mutex) return;

    cJSON *json = cJSON_CreateObject();
    const char *event_name = "device";

    switch (event_type) {
    case EVENT_DEVICE_FOUND:
        event_name = "device_found";
        if (device) {
            cJSON_AddItemToObject(json, "device", device_to_json((bt_device_info_t*)device));
        }
        break;
    case EVENT_SCAN_COMPLETE:
        event_name = "scan_complete";
        break;
    case EVENT_PAIR_SUCCESS:
        event_name = "pair_success";
        if (device) {
            cJSON_AddItemToObject(json, "device", device_to_json((bt_device_info_t*)device));
        }
        break;
    case EVENT_PAIR_FAILED:
        event_name = "pair_failed";
        break;
    case EVENT_UNPAIR_SUCCESS:
        event_name = "unpair_success";
        if (device) {
            cJSON_AddItemToObject(json, "device", device_to_json((bt_device_info_t*)device));
        }
        break;
    }

    if (message) {
        cJSON_AddStringToObject(json, "message", message);
    }

    const char *data = cJSON_PrintUnformatted(json);
    char *event_buf = malloc(strlen(data) + 64);
    if (event_buf) {
        sprintf(event_buf, "event: %s\ndata: %s\n\n", event_name, data);

        // 发送给所有客户端
        if (xSemaphoreTake(g_sse_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            for (int i = 0; i < g_sse_client_count; i++) {
                httpd_resp_sendstr_chunk(g_sse_clients[i], event_buf);
            }
            xSemaphoreGive(g_sse_mutex);
        }

        free(event_buf);
    }

    free((void*)data);
    cJSON_Delete(json);
}

esp_err_t http_server_start(void)
{
    if (g_server) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }

    // 创建互斥锁
    g_sse_mutex = xSemaphoreCreateMutex();
    if (!g_sse_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;

    ESP_LOGI(TAG, "Starting HTTP server on port %d", config.server_port);

    esp_err_t ret = httpd_start(&g_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return ret;
    }

    // 注册 URI 处理器
    httpd_uri_t root_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler
    };
    httpd_register_uri_handler(g_server, &root_uri);

    httpd_uri_t style_uri = {
        .uri = "/style.css",
        .method = HTTP_GET,
        .handler = style_handler
    };
    httpd_register_uri_handler(g_server, &style_uri);

    httpd_uri_t js_uri = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = js_handler
    };
    httpd_register_uri_handler(g_server, &js_uri);

    httpd_uri_t scan_uri = {
        .uri = "/api/scan",
        .method = HTTP_POST,
        .handler = scan_handler
    };
    httpd_register_uri_handler(g_server, &scan_uri);

    httpd_uri_t scan_stop_uri = {
        .uri = "/api/scan/stop",
        .method = HTTP_POST,
        .handler = scan_stop_handler
    };
    httpd_register_uri_handler(g_server, &scan_stop_uri);

    httpd_uri_t devices_uri = {
        .uri = "/api/devices",
        .method = HTTP_GET,
        .handler = devices_handler
    };
    httpd_register_uri_handler(g_server, &devices_uri);

    httpd_uri_t pair_uri = {
        .uri = "/api/pair",
        .method = HTTP_POST,
        .handler = pair_handler
    };
    httpd_register_uri_handler(g_server, &pair_uri);

    httpd_uri_t unpair_uri = {
        .uri = "/api/unpair",
        .method = HTTP_POST,
        .handler = unpair_handler
    };
    httpd_register_uri_handler(g_server, &unpair_uri);

    httpd_uri_t status_uri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = status_handler
    };
    httpd_register_uri_handler(g_server, &status_uri);

    httpd_uri_t events_uri = {
        .uri = "/api/events",
        .method = HTTP_GET,
        .handler = events_handler
    };
    httpd_register_uri_handler(g_server, &events_uri);

    ESP_LOGI(TAG, "HTTP server started");
    return ESP_OK;
}

esp_err_t http_server_stop(void)
{
    if (!g_server) {
        return ESP_OK;
    }

    esp_err_t ret = httpd_stop(g_server);
    if (ret == ESP_OK) {
        g_server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }

    return ret;
}