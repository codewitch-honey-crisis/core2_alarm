#ifndef INTERFACE_H
#define INTERFACE_H
#include <stdint.h>

static constexpr const size_t alarm_count = 4;
enum COMMAND_ID : uint8_t {
    SET_ALARM = 1, // followed by 1 byte, alarm id
    CLEAR_ALARM = 2, // followed by 1 byte, alarm id
    ALARM_THROWN = 3 // followed by 1 byte, alarm id
};

// must have <alarm_count> entries
static constexpr uint8_t slave_pins[alarm_count] = {
    27,14,12,13
};
#endif