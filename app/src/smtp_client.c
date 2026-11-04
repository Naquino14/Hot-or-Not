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

static int smtp_command(int sock, char* command, size_t buf_size, int expected_code, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int len = vsnprintf(command, buf_size, format, args);
    va_end(args);

    if (len < 0)
        return -EIO;
    if ((size_t)len >= buf_size) {
        LOG_ERR("command \"%.5s...\" too large: %d chars", command, len);
        return -E2BIG;
    }


    int ret = smtp_send_all(sock, command, (size_t)len);
    if (ret < 0) {
        LOG_ERR("Failed to send command \"%.5s%s\": %d", command, len > 5 ? "..." : "", ret);
        return ret;
    }

    int actual_code = 0;
    ret = smtp_read_resp(sock, expected_code, &actual_code);
    if (ret == -EIO) {
        LOG_WRN("SMTP server replied to command \"%.5s%s\" with unexpected code: %d (expected %d)", 
            command, len > 5 ? "..." : "", 
            actual_code, expected_code);
    } else if (ret < 0) {
        LOG_ERR("Error reading response: %d", ret);
        return ret;
    }

    return actual_code;
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
        goto cleanup;
    }

    ret = hon_tls_socket_cfg(sock, smtp_server_hostname);
    if (ret < 0 || !hon_tls_rdy()) {
        LOG_ERR("HON TLS not ready or not configured: %d", ret);
        goto cleanup;
    }
    
    ret = zsock_connect(sock, (struct sockaddr*) &smtp_server, sizeof(smtp_server));
    if (ret < 0) {
        ret = -errno;
        LOG_ERR("Failed to connect to SMTP server: %d", ret);
        goto cleanup;
    }

    int actual_code;

    // send hello for now
    ret = smtp_read_resp(sock, 220, &actual_code);
    if (ret < 0) {
        LOG_ERR("Invalid SMTP greeting: %d (code %d)", ret, actual_code);
        goto cleanup;
    }

    char command[CMDBUF_SIZE];

    ret = smtp_command(sock, command, sizeof(command), 250, "EHLO %s\r\n", smtp_server_hostname);
    if (ret < 0)
        goto cleanup;

    // auth. let auth borrow our buffer
    ret = smtp_auth_plain(sock, command);
    if (ret < 0)
        goto cleanup;

    // send email
    // mail from
    ret = smtp_command(sock, command, sizeof(command), 250, "MAIL FROM:<%s>\r\n", CONFIG_SMTP_USERNAME);
    if (ret < 0 && ret != 251) 
        goto cleanup;

    // receipt to
    ret = smtp_command(sock, command, sizeof(command), 250, "RCPT TO:<%s>\r\n", mail->to);
    if (ret < 0 && actual_code != 251)
        goto cleanup; 

    // send body
    ret = smtp_command(sock, command, sizeof(command), 354, "DATA\r\n");
    if (ret < 0)
        goto cleanup;

    // gmail does not require the time or ID parameters therefore they are not implemented here to save time
    const char* body_format = "From: %s\r\nTo: %s\r\nSubject: %s\r\n\r\n%s\r\n.\r\n";
    ret = smtp_command(sock, command, sizeof(command), 250, body_format, 
        CONFIG_SMTP_USERNAME,
        mail->to,
        mail->subject,
        mail->body);
    if (ret < 0 && ret != 251)
        goto cleanup;

    ret = smtp_command(sock, command, sizeof(command), 221, "QUIT\r\n");

cleanup:
    if (sock >= 0)
        zsock_close(sock);
    return ret;
}

int smtp_client_hello_auth() {
    if (smtp_server.sin_family == 0 || !rdy) {
        LOG_ERR("SMTP Server IP Address is not set.");
        return -EADDRNOTAVAIL;
    }

    int ret;
    int sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2);
    if (sock < 0) {
        ret = -errno;
        LOG_ERR("Failed to create TLS socket: %d", ret);
        goto cleanup;
    }

    ret = hon_tls_socket_cfg(sock, smtp_server_hostname);
    if (ret < 0 || !hon_tls_rdy()) {
        LOG_ERR("HON TLS not ready or not configured: %d", ret);
        goto cleanup;
    }
    
    ret = zsock_connect(sock, (struct sockaddr*) &smtp_server, sizeof(smtp_server));
    if (ret < 0) {
        ret = -errno;
        LOG_ERR("Failed to connect to SMTP server: %d", ret);
        goto cleanup;
    }

    int actual_code;

    // send hello for now
    ret = smtp_read_resp(sock, 220, &actual_code);
    if (ret < 0) {
        LOG_ERR("Invalid SMTP greeting: %d (code %d)", ret, actual_code);
        goto cleanup;
    }

    char command[CMDBUF_SIZE];

    ret = smtp_command(sock, command, sizeof(command), 250, "EHLO %s\r\n", smtp_server_hostname);
    if (ret < 0)
        goto cleanup;

    // auth. let auth borrow our buffer
    ret = smtp_auth_plain(sock, command);
    if (ret < 0)
        goto cleanup;

    ret = smtp_command(sock, command, sizeof(command), 221, "QUIT\r\n");

cleanup:
    if (sock >= 0)
        zsock_close(sock);
    return ret;
}