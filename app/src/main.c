#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "conn-mgr.h"

LOG_MODULE_REGISTER(main);

int main(void) {
    k_msleep(500);
    LOG_INF("Hello, Carlson!");

    conn_mgr_connect();
    return 0;
}
