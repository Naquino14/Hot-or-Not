#ifndef CONN_MGR_H
#define CONN_MGR_H

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

#endif // CONN_MGR_H