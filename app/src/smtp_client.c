#include "smtp_client.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#include "tls/hon_tls.h"

LOG_MODULE_REGISTER(smtp_client, LOG_LEVEL_INF); // CHANGEME: to LOG_LEVEL_ERR later

static struct net_sockaddr_in smtp_server = {
    .sin_family = 0 // 0 means not set yet
};
static char smtp_server_hostname[32];
static bool rdy;

#define ISNT_DIGIT(c) (c < '0' || c > '9')
#define TO_DIGIT(c) (c - '0')

static int smtp_send_all(int sock, const char* data, size_t len) {
    size_t off = 0;

    while (off < len) {
        ssize_t sent = zsock_send(sock, data + off, len - off, 0);

        if (sent < 0)
            return -errno;

        if (sent == 0)
            return -ECONNRESET;

        off += sent;
    }

    return 0;
}

#define MAX_LINE 128
static int smtp_read_resp(int sock, int expect_code, int* actual_code) {
    char line[MAX_LINE];
    int first_code = -1;

    for (;;) {
        ssize_t len = 0;

        while (len < sizeof(line) - 1) {
            char byte;
            ssize_t recvd = zsock_recv(sock, &byte, 1, 0);

            if (recvd < 0)
                return -errno;
            
            if (recvd == 0)
                return -ECONNRESET;

            line[len++] = byte;

            if (byte == '\n')
                break;
        }

        // check return code
        if (len < 4 || 
            ISNT_DIGIT(line[0]) ||
            ISNT_DIGIT(line[1]) ||
            ISNT_DIGIT(line[2]) ||
            (line[3] != ' ' && line[3] != '-'))
            return -EPROTO;
        
        int code = (TO_DIGIT(line[0]) * 100 + TO_DIGIT(line[1]) * 10 + TO_DIGIT(line[2]));

        if (first_code < 0)
            first_code = code;

        if (code != first_code)
            return -EPROTO;

        if (line[3] == '-')
            continue;

        if (actual_code)
            *actual_code = code;

        return first_code == expect_code ? 0 : -EIO;
    }
}

int smtp_set_server_ipaddr(const char *ipaddr, const char* hostname) {
    if (ipaddr == NULL || hostname == NULL)
        return -EINVAL;

    if (strlen(hostname) >= sizeof(smtp_server_hostname))
        return -EINVAL;

    int ret = net_addr_pton(AF_INET, ipaddr, &smtp_server.sin_addr);
    if (ret < 0) {
        LOG_ERR("Invalid IP address format: %s", ipaddr);
        return ret;
    }

    smtp_server.sin_family = AF_INET;
    smtp_server.sin_port = (uint16_t)htons(CONFIG_SMTP_PORT);
    strncpy(smtp_server_hostname, hostname, sizeof(smtp_server_hostname) - 1);
    smtp_server_hostname[sizeof(smtp_server_hostname) - 1] = '\0';

    rdy = true;
    return 0;
}

#define MAX_COMMAND 64
int smtp_client_send(const smtp_mail_t* mail) {
    if (smtp_server.sin_family == 0 || !rdy) {
        LOG_ERR("SMTP Server IP Address is not set.");
        return -EADDRNOTAVAIL;
    }

    if (mail == NULL || mail->to == NULL || mail->body == NULL || mail->subject == NULL)
        return -EINVAL;

    int ret;
    int sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2);
    if (sock < 0) {
        ret = -errno;
        LOG_ERR("Failed to create TLS socket: %d", ret);
        return ret;
    }

    ret = hon_tls_socket_cfg(sock, smtp_server_hostname);
    if (ret < 0 || !hon_tls_rdy()) {
        LOG_ERR("HON TLS not ready or not configured: %d", ret);
        return ret;
    }
    
    ret = zsock_connect(sock, (struct sockaddr*) &smtp_server, sizeof(smtp_server));
    if (ret < 0) {
        ret = -errno;
        LOG_ERR("Failed to connect to SMTP server: %d", ret);
        zsock_close(sock);
        return ret;
    }

    int actual_code;

    // send hello for now
    ret = smtp_read_resp(sock, 220, &actual_code);
    if (ret < 0) {
        LOG_ERR("Invalid SMTP greeting: %d (code %d)", ret, actual_code);
        zsock_close(sock);
        return ret;
    }

    char command[64];
    int len = snprintf(command, sizeof(command), "EHLO %s\r\n", smtp_server_hostname);
    if (len < 0 || len >= sizeof(command)) {
        LOG_ERR("EHLO command too large?");
        zsock_close(sock);
        return -EINVAL;
    }

    ret = smtp_send_all(sock, command, len);
    if (ret < 0) {
        LOG_ERR("Failed to send EHLO: %d", ret);
        zsock_close(sock);
        return -EINVAL;
    }

    // expect 250
    ret = smtp_read_resp(sock, 250, &actual_code);
    if (ret < 0) {
        LOG_ERR("EHLO Failed: %d (code %d)", ret, actual_code);
        zsock_close(sock);
        return ret;
    }

    len = snprintf(command, sizeof(command), "QUIT\r\n");
    ret = smtp_send_all(sock, command, len);
    if (ret < 0) {
        LOG_ERR("Failed to send QUIT: %d", ret);
        zsock_close(sock);
        return ret;
    }

    // quit is 221
    ret = smtp_read_resp(sock, 221, &actual_code);
    if (ret < 0) {
        LOG_ERR("QUIT failed: %d (code %d)", ret,  actual_code);
        zsock_close(sock);
        return ret;
    }

    zsock_close(sock);
    return 0;
}