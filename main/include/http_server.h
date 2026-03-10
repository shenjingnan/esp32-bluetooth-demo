/**
 * @file http_server.h
 * @brief HTTP 服务器模块接口
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 启动 HTTP 服务器
 *
 * @return ESP_OK 成功
 * @return 其他值 失败
 */
esp_err_t http_server_start(void);

/**
 * @brief 停止 HTTP 服务器
 *
 * @return ESP_OK 成功
 * @return 其他值 失败
 */
esp_err_t http_server_stop(void);

/**
 * @brief 发送 SSE 事件给所有连接的客户端
 *
 * @param event_type 事件类型
 * @param device 设备信息（可选）
 * @param message 消息（可选）
 */
void http_server_send_event(int event_type, void *device, const char *message);

#ifdef __cplusplus
}
#endif

#endif /* HTTP_SERVER_H */