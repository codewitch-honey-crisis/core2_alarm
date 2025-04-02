#include <Arduino.h>
#include "config.h"

static bool tripped[alarm_count];
static bool last[alarm_count];
void setup() {
    memset(tripped,0,sizeof(bool)*alarm_count);
#ifdef ESP_PLATFORM
    Serial2.begin(serial_baud_rate,SERIAL_8N1,slave_serial_pins.rx,slave_serial_pins.tx);
#else
    Serial2.begin(serial_baud_rate);
#endif
    for(size_t i = 0;i<alarm_count; ++i) {
        pinMode(alarm_switch_pins[i],INPUT);
        pinMode(alarm_enable_pins[i],OUTPUT);
    }
}
void loop() {
    uint8_t payload[2];
    for(size_t i = 0;i<alarm_count;++i) {
        const bool thrown = digitalRead(alarm_switch_pins[i])!=LOW;
        if(thrown!=last[i]) {
            last[i]=thrown;
            if(thrown && !tripped[i]) {
                payload[0]=ALARM_THROWN;
                payload[1]=i;
                Serial2.write(payload,sizeof(payload));
                Serial2.flush();
            }
        }
    }
    if(Serial2.available()>=2) {
        Serial2.readBytes((char*)payload,sizeof(payload));
        switch(payload[0]) {
            case SET_ALARM:
                if(payload[1]<alarm_count) {    
                    tripped[payload[1]]=true;
                    digitalWrite(alarm_enable_pins[payload[1]],HIGH);
                }
            break;
            case CLEAR_ALARM:
                if(payload[1]<alarm_count) {
                    tripped[payload[1]]=false;
                    digitalWrite(alarm_enable_pins[payload[1]],LOW);
                }
            break;
        }
    }
}