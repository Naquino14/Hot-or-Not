#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
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

    return 0;
}
