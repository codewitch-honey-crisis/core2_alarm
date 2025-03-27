#include <Arduino.h>
#include "interface.h"

static bool tripped[alarm_count];
static bool last[alarm_count];
void setup() {
    memset(tripped,0,sizeof(bool)*alarm_count);
    Serial2.begin(115200,SERIAL_8N1,16,17);
}
void loop() {
    for(int i = 0;i<alarm_count;++i) {
        const bool thrown = digitalRead(slave_pins[i])!=LOW;
        if(thrown!=last[i]) {
            last[i]=thrown;
            if(thrown && !tripped[i]) {
                uint8_t payload[2];
                payload[0]=ALARM_THROWN;
                payload[1]=i;
                Serial2.write(payload,sizeof(payload));
                Serial2.flush(true);
            }
        }
    }
    if(Serial2.available()>=2) {
        uint8_t payload[2];
        Serial2.read(payload,sizeof(payload));
        switch(payload[0]) {
            case SET_ALARM:
                if(payload[1]<alarm_count) {    
                    tripped[payload[1]]=true;
                }
            break;
            case CLEAR_ALARM:
                if(payload[1]<alarm_count) {
                    tripped[payload[1]]=false;
                }
            break;
        }
    }
}