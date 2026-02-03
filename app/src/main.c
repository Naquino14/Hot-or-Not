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
#define DNS_QUERY_TIMEOUT_MS 5000
#define DNS_QUERY_MAX_RETRIES 5

#define WIFI_CONNECT_TIMEOUT_MS 10000
#define WIFI_CONNECT_MAX_RETRIES 3

static bool dns_query_ok = false;
static char found_ip[MAX_IP_STR];
void query_cb(char* resolved_ip) {
    if (!dns_query_ok) {
        int len = strnlen(resolved_ip, MAX_IP_STR - 1);
        memcpy(found_ip, resolved_ip, len);
        found_ip[len] = '\0';
        LOG_INF("DNS Query successful! Resolved IP: %s", found_ip);
        dns_query_ok = true;
    } else
        LOG_WRN("Ignoring duplicate DNS response.");
}

int main(void) {
    k_msleep(500);
    LOG_INF("Hello, Carlson!");

    conn_mgr_init();

    // spin until connected
    int timeout_ms = WIFI_CONNECT_TIMEOUT_MS;
    int max_retries = WIFI_CONNECT_MAX_RETRIES;
    int attempt = 0;
    while (!conn_mgr_is_connected() && attempt < max_retries) {
        int ret = conn_mgr_connect();
        if (ret < 0)            
            LOG_WRN("Connection attempt failed with code %d", ret);

        LOG_INF("Waiting for connection... (attempt %d/%d)", attempt + 1, max_retries);
        k_msleep(timeout_ms / max_retries);
        attempt++;
    }

    if (!conn_mgr_is_connected()) {
        LOG_ERR("Could not connect to WiFi network after %d attempts, aborting tests.", max_retries);
        return -1;
    }

    // spin until we get dns resolution
    timeout_ms = DNS_QUERY_TIMEOUT_MS;
    max_retries = DNS_QUERY_MAX_RETRIES;
    attempt = 0; // reset attempt counter
    while (!dns_query_ok && attempt < max_retries) {
        LOG_INF("Performing DNS query... (attempt %d/%d)", attempt + 1, max_retries);
        conn_mgr_dns_query("google.com", query_cb);
        k_msleep(timeout_ms / max_retries);
        attempt++;
    }
    if (!dns_query_ok) {
        LOG_ERR("Could not resolve DNS after %d attempts, aborting tests.", max_retries);
        return -1;
    }

    // ping the resolved IP
    attempt = 0; // reset attempt counter
    while (!inet_ok && attempt < max_retries) {
        LOG_INF("Pinging %s... (attempt %d/%d)", found_ip, attempt + 1, max_retries);
        conn_mgr_ping_host(found_ip, ping_cb);
        k_msleep(timeout_ms / max_retries);
        attempt++;
    }

    if (!inet_ok) {
        LOG_ERR("Could not ping host after %d attempts, aborting tests.", max_retries);
        return -1;
    }

    LOG_INF("Network tests OK");

    return 0;
}
