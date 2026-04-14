#define DT_DRV_COMPAT asair_am2320

#include "am2320.h"

#include <zephyr/drivers/sensor.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <stdint.h>

LOG_MODULE_REGISTER(am2320);


struct am2320_config {
    const struct i2c_dt_spec i2c;
};

struct am2320_data {
    /// @brief Temperature in Degrees Celcius
    int16_t temp_c;

    /// @brief Humidity Percentage
    uint16_t humid_p;
};

static int am2320_sensor_sample_fetch(const struct device* dev, enum sensor_channel chan) {
    struct am2320_data* data = dev->data;

    switch (chan) {
        case SENSOR_CHAN_AMBIENT_TEMP:
            LOG_INF("Debug: temperature sample fetch");
            data->temp_c = 21; 
            break;
        case SENSOR_CHAN_HUMIDITY:
            LOG_INF("Debug: humidity sample fetch");
            data->humid_p = 50;
            break;
        default:
            LOG_ERR("Channel not supported.");
            return -ENOTSUP;
    }

    return 0;
}

static int am2320_sensor_channel_get(const struct device* dev, enum sensor_channel chan, struct sensor_value* val) {
    struct am2320_data* data = dev->data;

    val->val2 = 0;

    switch (chan) {
        case SENSOR_CHAN_AMBIENT_TEMP:
            LOG_INF("Debug: temperature fetch");
            val->val1 = data->temp_c;
            break;
        case SENSOR_CHAN_HUMIDITY:
            LOG_INF("Debug: humidity fetch");
            val->val1 = data->humid_p;
            break;
        default:
            LOG_ERR("Channel not supported.");
            return -ENOTSUP;
    }

    return 0;
}

static const struct sensor_driver_api am2320_api = {
    .channel_get = &am2320_sensor_channel_get,
    .sample_fetch = &am2320_sensor_sample_fetch
};

static int am2320_init(const struct device* dev) {
    LOG_INF("am2320 init");
    return 0;
}

#define AM2320_DEFINE(inst) \
    static struct am2320_data am2320_data_##inst;               \
                                                                \
    static struct am2320_config am2320_config_##inst = {        \
        .i2c = I2C_DT_SPEC_INST_GET(inst),                      \
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
