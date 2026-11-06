#ifndef HOTORNOT_TLS_H
#define HOTORNOT_TLS_H

#include <stdbool.h>

/**
 * Initializes the TLS system
 */
int hon_tls_init();

/**
 * Configures the TLS socket
 */
int hon_tls_socket_cfg(int sock, const char* hostname);

/**
 * Checks if Hot or Not board's TLS system is ready
 * @returns true when ready, false otherwise
 */
bool hon_tls_rdy();

#endif // HOTORNOT_TLS_H