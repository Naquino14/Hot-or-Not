#include "conn-mgr.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/wifi_mgmt.h>

// see sample:
// https://github.com/zephyrproject-rtos/zephyr/blob/main/samples/net/wifi/apsta_mode/src/main.c

LOG_MODULE_REGISTER(conn_mgr);

#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"

#define NET_EVENT_WIFI_MASK                                                   \
	    (NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |   \
	    NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_DISABLE_RESULT |  \
	    NET_EVENT_WIFI_AP_STA_CONNECTED | NET_EVENT_WIFI_AP_STA_DISCONNECTED)

// configs for wifi station mode
static struct net_if *sta_iface;
static struct wifi_connect_req_params sta_wifi_config;

// callbacks
static struct net_mgmt_event_callback net_mgmt_event_cb;

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t event_code, struct net_if *iface) {
    switch (event_code) {
        case NET_EVENT_WIFI_CONNECT_RESULT:
            LOG_INF("WiFi connected");
            break;
        case NET_EVENT_WIFI_DISCONNECT_RESULT:
            LOG_INF("WiFi disconnected");
        default: 
            break;
    }
}

#if !defined(CONFIG_WIFI_SSID)
#error CONFIG_WIFI_SSD not defined! Run `make menuconfig` and set a WIFI_SSID.
#endif 

#if !defined(CONFIG_WIFI_SECURITY)
#error CONFIG_WIFI_SECURITY not defined! Run `make menuconfig` and set CONFIG_WIFI_SECURITY.
#endif

BUILD_ASSERT(sizeof(CONFIG_WIFI_SSID) > 1, 
                "CONFIG_WIFI_SSID is empty! Run `make menuconfig` and set a WIFI_SSID");
                
                
static bool initd = false;

int conn_mgr_connect() {
    if (!initd) {
        LOG_INF("Performing first time setup of wifi iface");

        net_mgmt_init_event_callback(&net_mgmt_event_cb, wifi_event_handler, NET_EVENT_WIFI_MASK);
        net_mgmt_add_event_callback(&net_mgmt_event_cb);

        sta_iface = net_if_get_wifi_sta();

        if (sta_iface == NULL) {
            LOG_ERR("WiFi station interface not found.");
            return -ENETDOWN;
        }

        struct net_linkaddr *mac_addr = net_if_get_link_addr(sta_iface);
        initd = true;
        LOG_INF("First time WiFi iface setup complete. MAC: " MACSTR, 
                    mac_addr->addr[0], mac_addr->addr[1], mac_addr->addr[2],
                    mac_addr->addr[3], mac_addr->addr[4], mac_addr->addr[5]);
    }

    sta_wifi_config.ssid = (const uint8_t*)CONFIG_WIFI_SSID;
    sta_wifi_config.ssid_length = sizeof(CONFIG_WIFI_SSID) - 1;
    
#if defined(CONFIG_WIFI_PSK)
    if (sizeof(CONFIG_WIFI_PSK) > 1) {
        sta_wifi_config.psk = (const uint8_t*) CODE_UNREACHABLE
        sta_wifi_config.psk_length = sizeof(CONFIG_WIFI_PSK);
    }
#endif  

    sta_wifi_config.security = CONFIG_WIFI_SECURITY;
    sta_wifi_config.channel = WIFI_CHANNEL_ANY;
    sta_wifi_config.band = WIFI_FREQ_BAND_2_4_GHZ;

    LOG_INF("Attempting to connect to SSID %s", CONFIG_WIFI_SSID);

    int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, sta_iface, &sta_wifi_config, sizeof(struct wifi_connect_req_params));

    if (ret < 0) {
        LOG_ERR("Connection to SSID %s failed with code %d.", CONFIG_WIFI_SSID, ret);
    }

    return ret;
}
