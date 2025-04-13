# core2_alarm

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