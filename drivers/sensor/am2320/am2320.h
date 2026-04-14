#ifndef ZEPHYR_DRIVERS_SENSOR_AMIR_AM2320_H
#define ZEPHYR_DRIVERS_SENSOR_AMIR_AM2320_H

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>

/// Read one or more registers
#define AM2320_FC_READ_REG 0x03
/// Multiple sets of binary data to write to multiple registers 
#define AM2320_FC_WRITE_MULT 0x10

#define AM2320_REG_HUMIDITY_HIGH 0x00
#define AM2320_REG_HUMIDITY_LOW 0x01

#define AM2320_REG_TEMPERATURE_HIGH 0x02
#define AM2320_REG_TEMPERATURE_LOW 0x03

#define AM2320_REG_MODEL_HIGH 0x08
#define AM2320_REG_MODEL_LOW 0x09
#define AM2320_REG_VERSION 0x0b
#define AM2320_REG_ID_24_31 0x0b
#define AM2320_REG_ID_16_23 0x0c
#define AM2320_REG_ID_8_15 0x0d
#define AM2320_REG_ID_0_7 0x0e
/// Currently usless
#define AM2320_REG_STATUS 0x0f 

#define AM2320_REG_USER1_HIGH 0x18
#define AM2320_REG_USER1_LOW 0x19
#define AM2320_REG_USER2_HIGH 0x1a
#define AM2320_REG_USER2_LOW 0x1b

#endif // !ZEPHYR_DRIVERS_SENSOR_AMIR_AM2320_H