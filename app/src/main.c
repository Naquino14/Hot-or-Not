#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/icmp.h>

#include "conn-mgr.h"

LOG_MODULE_REGISTER(main);

// ping callback
bool inet_ok = false;
void ping_cb(int code) {
    if (code == 0) {
        LOG_INF("Ping successful!");
        inet_ok = true;
    } else {
        LOG_ERR("Ping failed with code %d", code);
    }
}

int main(void) {
    k_msleep(500);
    LOG_INF("Hello, Carlson!");

    conn_mgr_init();
    conn_mgr_connect();

    // spin until connected
    while (!conn_mgr_is_connected())
        k_msleep(100);

    conn_mgr_ping_host("one.one.one.one", ping_cb);

    while (!inet_ok)
        k_msleep(100);

    return 0;
}
