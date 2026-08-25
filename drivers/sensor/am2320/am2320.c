#define DT_DRV_COMPAT asair_am2320

#include "am2320.h"

#include <zephyr/drivers/sensor.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/crc.h>
#include <stdint.h>

LOG_MODULE_REGISTER(am2320, LOG_LEVEL_ERR);

struct am2320_config {
    const struct i2c_dt_spec i2c;
};

struct am2320_data {
    /// @brief Temperature in Degrees Celcius
    int16_t temp_c_tenths;

    /// @brief Humidity Percentage
    uint16_t humid_p_tenths;
};

static bool am2320_crc_ok(const uint8_t *response_frame, size_t frame_len) {
#ifndef CONFIG_AM2320_DO_CRC
    ARG_UNUSED(response_frame);
    ARG_UNUSED(frame_len);
    return true;
#else
    uint16_t crc_calc = crc16_ansi(response_frame, frame_len - 2);
    uint16_t crc_recv = ((uint16_t)response_frame[frame_len - 1] << 8) | response_frame[frame_len - 2];
    if (crc_calc != crc_recv) {
        LOG_ERR("CRC check failed: calc=0x%04x recv=0x%04x", crc_calc, crc_recv);
        return false;
    }
    return true;
#endif
}

static int am2320_sensor_sample_fetch(const struct device* dev, enum sensor_channel chan) {
    struct am2320_data* data = dev->data;
    const struct am2320_config* cfg = dev->config;

    // AM2320 needs to wake up before its used. 
    // Ignore ret
    uint8_t wakeywakey = 0x67;
    (void)i2c_write_dt(&cfg->i2c, &wakeywakey, 1);
    k_sleep(AM2320_WAKE_DELAY);

    int ret;
    uint8_t request_frame[3]; // [function code, high register addr, 2 bytes]
    // [function code, 2 bytes, high value, low value, high crc | high value 2, low crc | low value 2 [, high crc, low crc]]
    uint8_t response_frame[8]; 

    request_frame[0] = AM2320_FC_READ_REG;
    request_frame[2] = 2;

    switch (chan) {
        case SENSOR_CHAN_AMBIENT_TEMP:
            LOG_DBG("temperature sample fetch");

            // send request for temperature
            request_frame[1] = AM2320_REG_TEMPERATURE_HIGH;
            ret = i2c_write_dt(&cfg->i2c, request_frame, sizeof(request_frame));
            if (ret < 0) {
                LOG_ERR("Failed to request temperature register values: IO error.");
                return -EIO;
            }
            k_sleep(AM2320_REQUEST_DELAY);

            // read temperature
            ret = i2c_read_dt(&cfg->i2c, response_frame, sizeof(response_frame) - 2);
            if (ret < 0) {
                LOG_ERR("Failed to read temperature register values: IO error.");
                return -EIO;
            } else if (response_frame[0] != AM2320_FC_READ_REG || response_frame[1] != 2) {
                LOG_ERR("Bad temperature response header: fc=0x%02x len=%u", response_frame[0], response_frame[1]);
                return -EIO;
            }

            if (!am2320_crc_ok(response_frame, sizeof(response_frame) - 2))
                return -EAGAIN;

            uint16_t raw = (response_frame[2] << 8) | response_frame[3];
            bool sign = raw & 0x8000;
            raw &= ~0x8000;
            data->temp_c_tenths = (sign ? -raw : raw);
            
            break;
        case SENSOR_CHAN_HUMIDITY:
            LOG_DBG("humidity sample fetch");

            // send request for humidity
            request_frame[1] = AM2320_REG_HUMIDITY_HIGH;
            ret = i2c_write_dt(&cfg->i2c, request_frame, sizeof(request_frame));
            if (ret < 0) {
                LOG_ERR("Failed to request humidity register values: IO error.");
                return -EIO;
            }
            k_sleep(AM2320_REQUEST_DELAY);

            // read humidity
            ret = i2c_read_dt(&cfg->i2c, response_frame, sizeof(response_frame) - 2);
            if (ret < 0) {
                LOG_ERR("Failed to read humidity register values: IO error.");
                return -EIO;
            } else if (response_frame[0] != AM2320_FC_READ_REG || response_frame[1] != 2) {
                LOG_ERR("Bad humidity response header: fc=0x%02x len=%u", response_frame[0], response_frame[1]);
                return -EAGAIN;
            }

            if (!am2320_crc_ok(response_frame, sizeof(response_frame) - 2))
                return -EAGAIN;

            data->humid_p_tenths = (response_frame[2] << 8) | response_frame[3];

            break;
        case SENSOR_CHAN_ALL:
        case SENSOR_CHAN_AMBIENT_TEMP | SENSOR_CHAN_HUMIDITY:
        LOG_DBG("temperature and humidity sample fetch");
        
        // send request for both temperature and humidity
        request_frame[1] = AM2320_REG_HUMIDITY_HIGH;
        request_frame[2] = 4; // 2 bytes temp, 2 bytes humidity
        ret = i2c_write_dt(&cfg->i2c, request_frame, sizeof(request_frame));
        if (ret < 0) {
            LOG_ERR("Failed to request temperature humidity register values: IO error.");
            return -EIO;
        }
        k_sleep(AM2320_REQUEST_DELAY);

        // read temperature and humidity
        ret = i2c_read_dt(&cfg->i2c, response_frame, sizeof(response_frame));
        if (ret < 0) {
            LOG_ERR("Failed to read temperature and humidity register values: IO error.");
            return -EIO;
        } else if (response_frame[0] != AM2320_FC_READ_REG || response_frame[1] != 4) {
            LOG_ERR("Bad temperature and humidity response header: fc=0x%02x len=%u", response_frame[0], response_frame[1]);
            return -EAGAIN;
        }

        if (!am2320_crc_ok(response_frame, sizeof(response_frame)))
            return -EAGAIN;

        uint16_t temp_raw = (response_frame[4] << 8) | response_frame[5];
        bool temp_sign = temp_raw & 0x8000;
        temp_raw &= ~0x8000;
        data->temp_c_tenths = (temp_sign ? -temp_raw : temp_raw);

        data->humid_p_tenths = (response_frame[2] << 8) | response_frame[3];

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
            LOG_DBG("temperature get");
            val->val1 = data->temp_c_tenths / 10;
            val->val2 = data->temp_c_tenths % 10;
            break;
        case SENSOR_CHAN_HUMIDITY:
            LOG_DBG("humidity get");
            val->val1 = data->humid_p_tenths / 10;
            val->val2 = data->humid_p_tenths % 10;
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
    const struct am2320_config* cfg = dev->config;

    if (!i2c_is_ready_dt(&cfg->i2c)) {
        LOG_ERR("I2C bus not ready");
        return -ENODEV;
    }

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
