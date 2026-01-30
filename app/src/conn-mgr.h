#ifndef CONN_MGR_H
#define CONN_MGR_H

#include <stdbool.h>

/**
 * @brief init this bih
 */
int conn_mgr_init();

/**
 * @brief Connect to the configured WiFi network.
 * @returns 0 on success, negative error code on failure.
 */
int conn_mgr_connect();

/**
 * @brief Disconnect from the currently connected WiFi network.
 * @returns 0 on success, negative error code on failure.
 */
int conn_mgr_disconnect();

/**
 * @brief Check if currently connected to a WiFi network.
 * @returns true if connected, false otherwise.
 */
bool conn_mgr_is_connected();

/**
 * @typedef conn_mgr_ping_callback_t
 * @brief Callback function describing the status of a ping
 */
typedef void (*conn_mgr_ping_callback_t)(int code); 

/**
 * @brief Send an ICMP echo request to `hostname`.
 * @param hostname The hostname to ping.
 * @param cb Pointer to a callback function that will be called when a response is received.
 * @returns 0 on success, negative error code on failure.
 */
int conn_mgr_ping_host(const char* hostname, conn_mgr_ping_callback_t cb);

#endif // CONN_MGR_H