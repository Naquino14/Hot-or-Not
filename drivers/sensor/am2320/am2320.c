#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>

#define DT_DRV_COMPAT asair_am2320

LOG_MODULE_REGISTER(am2320);


struct am2320_config {

};

struct am2320_data {

};

static const struct sensor_driver_api am2320_api = {

};

static int am2320_init(const struct device* dev) {
    LOG_INF("am2320 init");
    return 0;
}

#define AM2320_DEFINE(inst) \
    static struct am2320_data am2320_data_##inst = {            \
                                                                \
    };                                                          \
                                                                \
    static struct am2320_config am2320_config_##inst = {        \
                                                                \
    };                                                          \
                                                                \
    SENSOR_DEVICE_DT_INST_DEFINE(inst,                          \
                                 am2320_init,                   \
                                 NULL,                          \ 
                                 &am2320_data_##inst,           \
                                 &am2320_config_##inst,         \ 
                                 POST_KERNEL,                   \
                                 CONFIG_SENSOR_INIT_PRIORITY,   \
                                 &am2320_api);

DT_INST_FOREACH_STATUS_OKAY(AM2320_DEFINE);
