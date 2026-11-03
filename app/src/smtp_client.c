#include "smtp_client.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <mbedtls/base64.h>
#include <time.h>

#include "tls/hon_tls.h"

LOG_MODULE_REGISTER(smtp_client, LOG_LEVEL_INF); // CHANGEME: to LOG_LEVEL_ERR later

static struct net_sockaddr_in smtp_server = {
    .sin_family = 0 // 0 means not set yet
};
static char smtp_server_hostname[32];
static bool rdy;

#define ISNT_DIGIT(c) (c < '0' || c > '9')
#define TO_DIGIT(c) (c - '0')

#define CMDBUF_SIZE 512

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

#define BASE64_LEN(len) (((len + 2) / 3) * 4 + 1)
static int smtp_auth_plain(int sock, char cmd_buf[CMDBUF_SIZE]) {
    const size_t uname_len = sizeof(CONFIG_SMTP_USERNAME) - 1;
    const size_t pass_len = sizeof(CONFIG_SMTP_PASSWORD) - 1;
    const size_t cred_len = uname_len + pass_len + 2; // null + uname + null + pass
    uint8_t cred[cred_len];
    char encoded[BASE64_LEN(cred_len) + 1]; // allat + string null
    
    cred[0] = '\0';
    memcpy(&cred[1], CONFIG_SMTP_USERNAME, uname_len);
    cred[uname_len + 1] = '\0';
    memcpy(&cred[uname_len + 2], CONFIG_SMTP_PASSWORD, pass_len);

    size_t enc_len = 0;
    int ret = mbedtls_base64_encode(encoded, sizeof(encoded) - 1, &enc_len, cred, cred_len);
    if (ret != 0) {
        LOG_ERR("Failed to encode SMTP creds: %d", ret);
        return -EIO;
    }

    encoded[enc_len] = '\0';

    int len = snprintf(cmd_buf, CMDBUF_SIZE, "AUTH PLAIN %s\r\n", encoded);
    if (len < 0 || len >= CMDBUF_SIZE) {
        LOG_ERR("SMTP AUTH command is too large: %u (sizeof(cmd_buf) is %u)", len, CMDBUF_SIZE);
        return -E2BIG;
    }

    ret = smtp_send_all(sock, cmd_buf, len);
    if (ret < 0) {
        LOG_ERR("Failed to send SMTP AUTH: %d", ret);
        return ret;
    }

    int actual_code;
    ret = smtp_read_resp(sock, 235, &actual_code);
    if (ret < 0) {
        LOG_ERR("SMTP auth failed: %d (code %d)", ret, actual_code);
        return ret;
    }

    return 0;
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

    char command[CMDBUF_SIZE];
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

    // auth. let auth borrow our buffer
    ret = smtp_auth_plain(sock, command);
    if (ret < 0) {
        zsock_close(sock);        
        return ret;
    } else
        LOG_INF("AUTH OK");

    // send email
    // mail from
    len = snprintf(command, sizeof(command), "MAIL FROM:<%s>\r\n", CONFIG_SMTP_USERNAME);
    ret = smtp_send_all(sock, command, len);
    if (ret < 0) {
        LOG_ERR("Failed to send MAIL FROM: %d", ret);
        zsock_close(sock);
        return ret;
    }

    ret = smtp_read_resp(sock, 250, &actual_code);
    if (ret < 0 && actual_code != 251) {
        LOG_ERR("MAIL FROM failed: %d (code %d)", ret, actual_code);
        zsock_close(sock);
        return ret;
    }

    // receipt to
    len = snprintf(command, sizeof(command), "RCPT TO:<%s>\r\n", mail->to);
    ret = smtp_send_all(sock, command, len);
    if (ret < 0) {
        LOG_ERR("Failed to send RCPT TO: %d", ret);
        zsock_close(sock);
        return ret;
    }

    ret = smtp_read_resp(sock, 250, &actual_code);
    if (ret < 0 && actual_code != 251) {
        LOG_ERR("RCPT TO failed: %d (code %d)", ret, actual_code);
        zsock_close(sock);
        return ret;
    }

    // send body
    len = snprintf(command, sizeof(command), "DATA\r\n");
    ret = smtp_send_all(sock, command, len);
    if (ret < 0) {
        LOG_ERR("Failed to send DATA: %d", ret);
        zsock_close(sock);
        return ret;
    }

    ret = smtp_read_resp(sock, 354, &actual_code);
    if (ret < 0) {
        LOG_ERR("DATA failed: %d (code %d)", ret, actual_code);
        zsock_close(sock);
        return ret;
    }

    // gmail does not require the time or ID parameters therefore they are not implemented here to save time
    const char* body_format = "From: %s\r\nTo: %s\r\nSubject: %s\r\n\r\n%s\r\n.\r\n";
    
    len = snprintf(command, sizeof(command), body_format,
        CONFIG_SMTP_USERNAME,
        mail->to,
        mail->subject,
        mail->body);
    if (len < 0 || len >= sizeof(command)) {
        LOG_ERR("Email too large: %d chars", len);
        zsock_close(sock);
        return -EINVAL;
    }
    ret = smtp_send_all(sock, command, len);
    if (ret < 0) {
        LOG_ERR("Failed to send DATA: %d", ret);
        zsock_close(sock);
        return ret;
    }

    ret = smtp_read_resp(sock, 250, &actual_code);
    if (ret < 0 && actual_code != 251) {
        LOG_ERR("DATA body failed: %d (code %d)", ret, actual_code);
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