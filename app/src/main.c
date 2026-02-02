#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/icmp.h>
#include <string.h>

#include "conn-mgr.h"

LOG_MODULE_REGISTER(main);

// ping callback
static bool inet_ok = false;
void ping_cb(int code) {
    if (code == 0) {
        LOG_INF("Ping successful!");
        inet_ok = true;
    } else {
        LOG_ERR("Ping failed with code %d", code);
    }
}


#define MAX_IP_STR 16
#define DNS_QUERY_TIMEOUT_MS 3000

static bool dns_query_ok = false;
static char found_ip[MAX_IP_STR];
void query_cb(char* resolved_ip) {
    int len = strlen(resolved_ip);
    memcpy(found_ip, resolved_ip, len);
    found_ip[len] = '\0';
    LOG_INF("DNS Query successful! Resolved IP: %s", found_ip);
    dns_query_ok = true;
}

int main(void) {
    k_msleep(500);
    LOG_INF("Hello, Carlson!");

    conn_mgr_init();
    conn_mgr_connect();

    // spin until connected
    while (!conn_mgr_is_connected())
        k_msleep(100);

    const char* test_hostname = "google.com";
    conn_mgr_dns_query(test_hostname, query_cb);
    while (!dns_query_ok)
        k_msleep(100);

    conn_mgr_ping_host(found_ip, ping_cb);

    while (!inet_ok)
        k_msleep(100);

    LOG_INF("Network tests OK");

    return 0;
}
