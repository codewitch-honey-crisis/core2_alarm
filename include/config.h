#ifndef CONFIG_H
#define CONFIG_H
#include <stdint.h>

static constexpr const size_t alarm_count = 4;
enum COMMAND_ID : uint8_t {
    SET_ALARM = 1, // followed by 1 byte, alarm id
    CLEAR_ALARM = 2, // followed by 1 byte, alarm id
    ALARM_THROWN = 3 // followed by 1 byte, alarm id
};

// The fire alarm switches - must have <alarm_count> entries
static constexpr uint8_t alarm_switch_pins[alarm_count] = {
    27,14,12,13
};
// The fire alarm enable pins - must have <alarm_count> entries
static constexpr uint8_t alarm_enable_pins[alarm_count] = {
    28,9,10,11
};

static constexpr unsigned long int serial_baud_rate = 115200;

// the pins used for serial transmission to the slave
static constexpr struct {
    uint8_t rx;
    uint8_t tx;
} control_serial_pins = {32,33};

#ifdef ESP_PLATFORM
// the pins used for serial transmission from the slave
// only for ESP32
constexpr struct {
    uint8_t rx;
    uint8_t tx;
} slave_serial_pins = {16,17};
#endif
#endif