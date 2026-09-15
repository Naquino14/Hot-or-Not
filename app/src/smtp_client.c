#include "smtp_client.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

LOG_MODULE_REGISTER(smtp_client, LOG_LEVEL_INF); // CHANGEME: to LOG_LEVEL_ERR later

static struct net_sockaddr_in smtp_server = {
    .sin_family = 0 // 0 means not set yet
};

void smtp_set_server_ipaddr(const char *ipaddr) {
    int ret = net_addr_pton(AF_INET, ipaddr, &smtp_server.sin_addr);
    if (ret < 0) {
        LOG_ERR("Invalid IP address format: %s", ipaddr);
        return;
    }
}

int smtp_client_send(const smtp_mail_t* mail) {
    if (smtp_server.sin_family == 0) {
        LOG_ERR("SMTP Server IP Address is not set.");
        return -EADDRNOTAVAIL;
    }
}