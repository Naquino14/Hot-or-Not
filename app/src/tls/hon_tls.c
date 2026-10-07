#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <mbedtls/pem.h>
#include <limits.h>

#include "hon_tls.h"

LOG_MODULE_REGISTER(hon_tls);

#include "ca_certificate.h"
static mbedtls_pem_context pem_ctx = {0};
static size_t der_len = INT_MAX;

#define PEM_HEADER "-----BEGIN CERTIFICATE-----"
#define PEM_FOOTER "-----END CERTIFICATE-----"

static bool rdy = false;

int hon_tls_init() {
    // init ctx
    mbedtls_pem_init(&pem_ctx);

    // convert PEM to DER
    int ret = mbedtls_pem_read_buffer(
        &pem_ctx,
        PEM_HEADER, PEM_FOOTER,
        smtp_ca_certificate, 
        NULL, 0, // no password
        &der_len
    );

    if (ret == MBEDTLS_ERR_PEM_NO_HEADER_FOOTER_PRESENT) {
        LOG_ERR("Failed to parse CA cert: No header or footer found.");
        return ret;
    }

    const char parse_fail[] = "Failed to parse CA cert: ";

    switch (ret) {
        case MBEDTLS_ERR_PEM_NO_HEADER_FOOTER_PRESENT:
            LOG_ERR("%sNo header or footer found.", parse_fail);
            return ret;
        case MBEDTLS_ERR_PEM_INVALID_DATA:
            LOG_ERR("%sInvalid PEM data.", parse_fail);
            return ret;
        default:
            if (ret < 0) {
                LOG_ERR("%sUnknown error: %d", parse_fail, ret);
                return ret;
            }
            break;
    }

    ret = tls_credential_add(
        CA_CERTIFICATE_TAG,
        TLS_CREDENTIAL_CA_CERTIFICATE,
        smtp_ca_certificate,
        sizeof(smtp_ca_certificate) - 1
    );
    if (ret < 0) {
        LOG_ERR("Could not add CA Certificate to TLS system: %d", ret);
        return ret;
    }

    rdy = true;
    return 0;
}

int hon_tls_socket_cfg(int sock, const char* hostname) {
    if (!rdy)
        return -EAGAIN;

    sec_tag_t sec_tag_list[] = { CA_CERTIFICATE_TAG };
    
    if (sock < 0 || hostname == NULL || *hostname == 0)
        return -EINVAL;

    int ret = zsock_setsockopt(
        sock, 
        SOL_TLS,
        TLS_SEC_TAG_LIST,
        sec_tag_list,
        sizeof(sec_tag_list)
    );
    if (ret < 0) {
        ret = -errno;
        LOG_ERR("Failed to configure TLS credentials for %s on socket %d: %d", hostname, sock, ret);
        return ret;
    }

    int peer_verify = TLS_PEER_VERIFY_REQUIRED;
    ret = zsock_setsockopt(
        sock,
        SOL_TLS,
        TLS_PEER_VERIFY,
        &peer_verify,
        sizeof(peer_verify)
    );
    if (ret < 0) {
        ret = -errno;
        LOG_ERR("Failed to configure TLS peer verification for %s on socket %d: %d", hostname, sock, ret);
        return ret;
    }

    ret = zsock_setsockopt(
        sock,
        SOL_TLS,
        TLS_HOSTNAME,
        hostname,
        strlen(hostname) + 1
    );
    if (ret < 0) {
        ret = -errno;
        LOG_ERR("Failed to configure TLS hostname for %s on socket %d: %d", hostname, sock, ret);
        return ret;
    }
    
    return 0;
}

bool hon_tls_rdy() {
    return rdy;
}