#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/icmp.h>
#include <string.h>
#include <zephyr/drivers/sensor.h>

#include <conn_mgr.h>

#include "smtp_client.h"
#include "hon_sntp.h"
#include "tls/hon_tls.h"

LOG_MODULE_REGISTER(main);

// get am2320 device
#define TANDH_NODE DT_ALIAS(tandh)
const struct device* dev_tandh = DEVICE_DT_GET(TANDH_NODE);

// ping callback
static volatile bool ping_done;
static volatile bool inet_ok;

void ping_cb(int code) {
    ping_done = true;

    if (code == 0) {
        LOG_INF("Ping successful!");
        inet_ok = true;
    } else {
        LOG_ERR("Ping failed with code %d", code);
    }
}


#define MAX_IP_STR 16
#define DNS_QUERY_TIMEOUT_MS 12000
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

/// TODO: Turn this into a thread
#define SNTP_RESYNC_PERIOD K_HOURS(24)
static struct k_work_delayable sntp_resync_dwork;
static void sntp_resync_handler(struct k_work* work) {
    int ret = hon_sntp_get_time();
    if (ret < 0) 
        LOG_ERR("RESYNC FAILED: (%d)! TOO BAD!", ret);
        
    k_work_reschedule(&sntp_resync_dwork, SNTP_RESYNC_PERIOD);
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

    conn_mgr_enable_watchdog();

    // spin until we get dns resolution
    timeout_ms = DNS_QUERY_TIMEOUT_MS;
    max_retries = DNS_QUERY_MAX_RETRIES;
    attempt = 0; // reset attempt counter
    while (!dns_query_ok && attempt < max_retries) {
        LOG_INF("Performing DNS query of %s... (attempt %d/%d)", CONFIG_SMTP_SERVER, attempt + 1, max_retries);
        conn_mgr_dns_query(CONFIG_SMTP_SERVER, query_cb);
        k_msleep(timeout_ms / max_retries);
        attempt++;
    }
    if (!dns_query_ok) {
        LOG_ERR("Could not resolve IP for %s after %d attempts, aborting tests.", CONFIG_SMTP_SERVER, max_retries);
        return -1;
    }

    // ping the resolved IP
    attempt = 0;
    while (!inet_ok && attempt < max_retries) {
        ping_done = false;

        LOG_INF("Pinging %s... (attempt %d/%d)",
                found_ip, attempt + 1, max_retries);

        int ret = conn_mgr_ping_host(found_ip, ping_cb);
        if (ret < 0) {
            LOG_ERR("Could not start ping: %d", ret);
            break;
        }

        int elapsed = 0;
        while (!ping_done && elapsed < timeout_ms) {
            k_msleep(100);
            elapsed += 100;
        }

        if (!ping_done)
            LOG_WRN("Ping timed out waiting for callback");

        attempt++;
    }

    if (!inet_ok) {
        LOG_ERR("Could not ping host after %d attempts, aborting tests.", max_retries);
        return -1;
    }

    // set smtp address
    smtp_set_server_ipaddr(found_ip, CONFIG_SMTP_SERVER);

    // get sntp address
    attempt = 0; // reset attempt counter
    dns_query_ok = false; // reset ok flag
    while (!dns_query_ok && attempt < max_retries) {
        LOG_INF("Performing DNS query of %s... (attempt %d/%d)", CONFIG_SNTP_SERVER, attempt + 1, max_retries);
        conn_mgr_dns_query(CONFIG_SNTP_SERVER, query_cb);
        k_msleep(timeout_ms / max_retries);
        attempt++;
    }
        if (!dns_query_ok) {
        LOG_ERR("Could not resolve IP for %s after %d attempts, aborting tests.", CONFIG_SNTP_SERVER, max_retries);
        return -1;
    }

    hon_sntp_set_server_ipaddr(found_ip);

    // enable sntp timer, run once now and do every 24 hours
    k_work_init_delayable(&sntp_resync_dwork, sntp_resync_handler);
    k_work_reschedule(&sntp_resync_dwork, K_NO_WAIT);

    LOG_INF("Waiting for first-time time sync...");
    while (!hon_sntp_ok())
        k_msleep(100);

    LOG_INF("Network tests OK, starting SMTP test");

    k_msleep(1000);

    int ret = hon_tls_init();
    if (ret < 0) {
        LOG_ERR("Failed to start TLS");
        return 1;
    } else 
        LOG_INF("TLS Started");

    // dummy mail while testing hello
    const smtp_mail_t mail = {
        .body = "Testing testing 123\r\nThis is a test of the refactored SMTP client\r\tSorry for email spam - Nate",
        .to = "help@cis.rit.edu",
        .subject = "Hello from ESP32 | Code refactored"
    };
    ret = smtp_client_send(&mail);
    if (ret < 0) {
        LOG_ERR("Shits fucked: %d", ret);
        return 1;
    }

    LOG_INF("SMTP tests OK");
    
    if (!device_is_ready(dev_tandh)) {
        LOG_ERR("Ah breh ts dont work");
        return 1;
    } else {
        LOG_INF("Found temperature and humidity sensor!");
    }

    for (;;) {
        double temp = -1;
        double humid = -1;

        k_sleep(K_SECONDS(CONFIG_SENSOR_SAMPLE_RATE));

#if defined(CONFIG_SENSOR_READ_ALL_CHANNELS)
        ret = sensor_sample_fetch_chan(dev_tandh, SENSOR_CHAN_ALL);
        if (ret < 0) {
            LOG_ERR("Fetch all channels failed: %d", ret);
            continue;
        }
#else
        ret = sensor_sample_fetch_chan(dev_tandh, SENSOR_CHAN_AMBIENT_TEMP);
        if (ret < 0) {
            LOG_ERR("Get ambient temp failed: %d", ret);
            continue;
        }

        k_msleep(2000);

        ret = sensor_sample_fetch_chan(dev_tandh, SENSOR_CHAN_HUMIDITY);
        if (ret < 0) {
            LOG_ERR("Get humidity failed: %d", ret);
            continue;
        }
#endif

        k_msleep(1);

        struct sensor_value temp_val;
        ret = sensor_channel_get(dev_tandh, SENSOR_CHAN_AMBIENT_TEMP, &temp_val);
        if (ret < 0) {
            LOG_ERR("Get ambient temp failed: %d", ret); 
            continue;
        }
        temp = temp_val.val1 + temp_val.val2 / 10.0;

        struct sensor_value humid_val;
        ret = sensor_channel_get(dev_tandh, SENSOR_CHAN_HUMIDITY, &humid_val);
        if (ret < 0) {  
            LOG_ERR("Get humidity failed: %d", ret); 
            continue;
        }
        humid = humid_val.val1 + humid_val.val2 / 10.0;

        double temp_f = temp * 9 / 5 + 32;

        LOG_INF("Temperature: %.1f C (%.1f F), Humidity: %.1f %%", temp, temp_f, humid);
    }

    return 0;
}
