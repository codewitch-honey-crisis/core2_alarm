# core2_alarm

Core2 Alarm is a fire alarm system control panel and fire alarm driver for an ESP32. It was made to simulate a commercial fire alarm system, for a friend's kid.

The application has two orchestrations, one for Arduino and one for the ESP-IDF. The ESP-IDF one is complete. The Arduino one is functionally complete but is provided for learning and not as fancy.

In terms of the ESP-IDF offering, it demonstrates

1. HTTPD use
2. A quick and dirty technique to feed wifi credentials to the device via SD or SPIFFS
3. Managing a WiFi connection
4. Generating and using dynamic web content with ClASP-Tree 
5. Driving a device over serial
6. Using htcw_gfx and htcw_uix with the ESP LCD Panel API to present a user interface.

## Setup

1. Set the appropriate COM ports in platformio.ini 

2. (Optional - necessary for web interface) Add wifi.txt to an SD or spiffs to connect to the network. First line is the SSID, second is the network password

3. Configure include/config.h for the count of alarms and all the associated pins

4. Upload Filesystem Image to control (only necessary for Arduino or if using wifi.txt in SPIFFS)

5. Upload Firmware to control

6. Upload Firmware to slave


## Web interface
The QR Link provides a QR code to get to the website. The JSON/REST api is located at ./api at the same location

a JSON example:

`http://192.168.50.14/api`

Where the IP is replaced with the local network IP of the Core2

```json
{
    "status": [
        true, false, true, true
    ]
}
```
Each boolean value in the status array is the state of a given alarm at the index.

The query string works the same for the web page as it does for the API:

Query string parameters:

- `a` = zero based alarm index) ex: `a=1` (only honored when `set` is specified)
- `set` = the presence of this parameter indicates that all `a` values be set, and any not present be cleared.

Example: `http://192.168.50.14?a=0&a=2&set`

Where the IP is replaced with the local network IP of the Core2

This will set all the fire alarms to off except #1 (zero based index of 0) and #3 (index 2)

### Note: The ESP-IDF version is slightly better, being a bit more elegant in terms of handling lots of alarms, plus being generally more efficient.

The HTTP responses in the ESP-IDF code were generated using ClASP: https://github.com/codewitch-honey-crisis/clasp

You can regenerate the clasp files from the project directory with the following command, but it is done on build anyway

```
clasptree web .\include\httpd_content.h /prefix httpd_ /epilogue .\include\httpd_epilogue.h /state resp_arg /block httpd_send_block /expr httpd_send_expr /handlers extended
```