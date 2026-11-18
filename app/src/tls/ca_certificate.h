#include <zephyr/autoconf.h>
#include <stdint.h>

#define CA_CERTIFICATE_TAG 67

static const uint8_t smtp_ca_certificate[] = {
#include CA_PEM_INC_FILENAME
    0x0
};