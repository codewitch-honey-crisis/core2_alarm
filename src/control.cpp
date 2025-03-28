// uncomment this to disable wifi and speed upload times:
//#define NO_WIFI
#include <Arduino.h>
#ifdef M5STACK_CORE2
#include <esp_i2c.hpp> // i2c initialization
#include <m5core2_power.hpp> // AXP192 power management (core2)
#endif
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ili9342.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <esp_vfs_fat.h>
#ifndef NO_WIFI
#include <sdmmc_cmd.h>
#include <SPIFFS.h>
#include <AsyncTCP.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#endif
#ifdef M5STACK_CORE2
#include <ft6336.hpp>
#endif
#include <uix.hpp> // user interface library
#include <gfx.hpp> // graphics library
// font is a TTF/OTF from downloaded from fontsquirrel.com
// converted to a header with https://honeythecodewitch.com/gfx/converter
#define OPENSANS_REGULAR_IMPLEMENTATION
#include "assets/OpenSans_Regular.h" // our font

#include "config.h"
// namespace imports
using namespace arduino; // devices
using namespace gfx; // graphics
using namespace uix; // user interface
using color_t = color<rgb_pixel<16>>;
using color32_t = color<rgba_pixel<32>>;

static const_buffer_stream font_stream(OpenSans_Regular,sizeof(OpenSans_Regular));
static tt_font text_font(font_stream,20,font_size_units::px);
#ifndef NO_WIFI

#define HTML_INPUT_FORMAT "            <label>%d</label><input name=\"a\" type=\"checkbox\" value=\"%d\" %s/><br />\n"
AsyncWebServer httpd(80);
#endif
#ifdef M5STACK_CORE2
using power_t = m5core2_power;
// for AXP192 power management
static power_t power(esp_i2c<1,21,22>::instance);
#endif

// for the touch panel
#ifdef M5STACK_CORE2
using touch_t = ft6336<320,280>;
static touch_t touch(esp_i2c<1,21,22>::instance);
#endif

using screen_t = uix::screen<rgb_pixel<16>>;

static uix::display lcd;

// handle to the display
static esp_lcd_panel_handle_t lcd_handle;
static constexpr const size_t lcd_transfer_buffer_size = 320*240*2/10;
// the transfer buffers
static uint8_t* lcd_transfer_buffer1 = nullptr;
static uint8_t* lcd_transfer_buffer2 = nullptr;

// tell UIX the DMA transfer is complete
static bool lcd_flush_ready(esp_lcd_panel_io_handle_t lcd_io, 
                                esp_lcd_panel_io_event_data_t* edata, 
                                void* user_ctx) {
    lcd.flush_complete();
    return true;
}
// tell the lcd panel api to transfer data via DMA
static void lcd_on_flush(const rect16& bounds, const void* bmp, void* state) {
    int x1 = bounds.x1, y1 = bounds.y1, x2 = bounds.x2 + 1, y2 = bounds.y2 + 1;
    esp_lcd_panel_draw_bitmap(lcd_handle, x1, y1, x2, y2, (void*)bmp);
}

// for the touch panel
static void lcd_on_touch(point16* out_locations,
                            size_t* in_out_locations_size,
                            void* state) {
    // UIX supports multiple touch points. 
    // so does the FT6336 so we potentially have
    // two values
    *in_out_locations_size = 0;
    uint16_t x,y;
    if(touch.xy(&x,&y)) {
        out_locations[0]=point16(x,y);
        ++*in_out_locations_size;
        if(touch.xy2(&x,&y)) {
            out_locations[1]=point16(x,y);
            ++*in_out_locations_size;
        }
    }
}
static void spi_init() {
    spi_bus_config_t buscfg;
    memset(&buscfg, 0, sizeof(buscfg));
    buscfg.sclk_io_num = 18;
    buscfg.mosi_io_num = 23;
    buscfg.miso_io_num = 38;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = gfx::math::max(lcd_transfer_buffer_size,(size_t)(16*1024)) + 8;
    // Initialize the SPI bus on VSPI (SPI3)
    spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
}

// initialize the screen using the esp panel API
static void lcd_init() {
    lcd_transfer_buffer1 = (uint8_t*)heap_caps_malloc(lcd_transfer_buffer_size,MALLOC_CAP_DMA);
    lcd_transfer_buffer2 = (uint8_t*)heap_caps_malloc(lcd_transfer_buffer_size,MALLOC_CAP_DMA);
    if(lcd_transfer_buffer1==nullptr||lcd_transfer_buffer2==nullptr) {
        puts("Out of memory allocating transfer buffers");
        while(1) vTaskDelay(5);
    }
    
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config;
    memset(&io_config, 0, sizeof(io_config));
    io_config.dc_gpio_num = 15,
    io_config.cs_gpio_num = 5,
    io_config.pclk_hz = 40*1000*1000,
    io_config.lcd_cmd_bits = 8,
    io_config.lcd_param_bits = 8,
    io_config.spi_mode = 0,
    io_config.trans_queue_depth = 10,
    io_config.on_color_trans_done = lcd_flush_ready;
    // Attach the LCD to the SPI bus
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI3_HOST, &io_config, &io_handle);

    lcd_handle = NULL;
    esp_lcd_panel_dev_config_t lcd_config;
    memset(&lcd_config, 0, sizeof(lcd_config));
    lcd_config.reset_gpio_num = -1;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    lcd_config.rgb_endian = LCD_RGB_ENDIAN_BGR;
#else
    lcd_config.color_space = ESP_LCD_COLOR_SPACE_BGR;
#endif
    lcd_config.bits_per_pixel = 16;

    // Initialize the LCD configuration
    esp_lcd_new_panel_ili9342(io_handle, &lcd_config, &lcd_handle);

    // Reset the display
    esp_lcd_panel_reset(lcd_handle);

    // Initialize LCD panel
    esp_lcd_panel_init(lcd_handle);
    esp_lcd_panel_swap_xy(lcd_handle, false);
    esp_lcd_panel_set_gap(lcd_handle, 0, 0);
    esp_lcd_panel_mirror(lcd_handle, false, false);
    esp_lcd_panel_invert_color(lcd_handle, true);
    // Turn on the screen
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_lcd_panel_disp_on_off(lcd_handle, true);
#else
    esp_lcd_panel_disp_off(lcd_handle, false);
#endif
    lcd.buffer_size(lcd_transfer_buffer_size);
    lcd.buffer1(lcd_transfer_buffer1);
    lcd.buffer2(lcd_transfer_buffer2);
    lcd.on_flush_callback(lcd_on_flush);
    lcd.on_touch_callback(lcd_on_touch);
    touch.initialize();
    touch.rotation(0);
    
}

using button_t = vbutton<screen_t::control_surface_type>;
using switch_t = vswitch<screen_t::control_surface_type>;
using label_t = label<screen_t::control_surface_type>;

// the screen/control definitions
static screen_t main_screen;
static button_t clear_all;
static button_t web_link;
static constexpr size_t switches_count = alarm_count;
static switch_t switches[switches_count];
static label_t switch_labels[switches_count];
static char switch_text[switches_count][4];

#ifndef NO_WIFI
static sdmmc_card_t *card = nullptr;
static bool sd_init() {
    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config;
    memset(&mount_config,0,sizeof(mount_config));
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 0;
    
    static const char mount_point[] = "/sdcard";
    sdmmc_host_t host;
    memset(&host,0,sizeof(host));

    host.flags = SDMMC_HOST_FLAG_SPI | SDMMC_HOST_FLAG_DEINIT_ARG;
    host.slot = SPI3_HOST;
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;
    host.io_voltage = 3.3f;
    host.init = &sdspi_host_init;
    host.set_bus_width = NULL;
    host.get_bus_width = NULL;
    host.set_bus_ddr_mode = NULL;
    host.set_card_clk = &sdspi_host_set_card_clk;
    host.set_cclk_always_on = NULL;
    host.do_transaction = &sdspi_host_do_transaction;
    host.deinit_p = &sdspi_host_remove_device;
    host.io_int_enable = &sdspi_host_io_int_enable;
    host.io_int_wait = &sdspi_host_io_int_wait;
    host.command_timeout_ms = 0;
    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    sdspi_device_config_t slot_config;
    memset(&slot_config,0,sizeof(slot_config));
    slot_config.host_id   = (spi_host_device_t)host.slot;
    slot_config.gpio_cs   = (gpio_num_t)4;
    slot_config.gpio_cd   = SDSPI_SLOT_NO_CD;
    slot_config.gpio_wp   = SDSPI_SLOT_NO_WP;
    slot_config.gpio_int  = GPIO_NUM_NC;
    esp_err_t ret;
    if(ESP_OK!=(ret=esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &card))) {
        puts(esp_err_to_name(ret));
        return false;
    }
    return true;
}
static char www_input_buffer[32*1024];
static void www_init() {
    httpd.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        const size_t params = request->params();
        if(params>0&&request->hasParam("set")) {
            for(size_t i = 0;i<alarm_count;++i) {
                switches[i].value(false);
            }
            for(size_t i = 0;i<params;++i) {
                auto p = request->getParam(i);
                if(p->name()=="a") {
                    const size_t sw = atoi(p->value().c_str());
                    if(sw<alarm_count) {
                        switches[sw].value(true);
                    }
                }
            }
        }
        www_input_buffer[0]=0;
        request->send(SPIFFS, "/index.thtml", "text/html", false, [](const String &var) -> String {
          char* sz = www_input_buffer;
          if (var == "ALARMS") {
            for(int i = 0;i<alarm_count;++i) {
                const bool checked = switches[i].value();
                sprintf(strlen(sz)+sz,HTML_INPUT_FORMAT,i+1,i,checked?"checked":"");
            }
          }
          return sz;
        });
      });
      httpd.on("/api", HTTP_GET, [](AsyncWebServerRequest *request) {
        const size_t params = request->params();
        if(params>0&&request->hasParam("set")) {
            for(size_t i = 0;i<alarm_count;++i) {
                switches[i].value(false);
            }
            for(size_t i = 0;i<params;++i) {
                auto p = request->getParam(i);
                if(p->name()=="a") {
                    const size_t sw = atoi(p->value().c_str());
                    if(sw<alarm_count) {
                        switches[sw].value(true);
                    }
                }
            }
        }
        www_input_buffer[0]=0;
        request->send(SPIFFS, "/api.tjson", "application/json", false, [](const String &var) -> String {
          char* sz = www_input_buffer;
          if (var == "ALARMS") {
            for(int i = 0;i<alarm_count;++i) {
                if(i>0) {
                    strcat(strlen(sz)+sz,", ");
                } else {
                    strcpy(sz,"        ");
                }
                const bool checked = switches[i].value();
                strcat(strlen(sz)+sz,checked?"true":"false");
            }
          }
          return sz;
        });
      });
      httpd.begin();    
}
#endif

static screen_t qr_screen;
using qr_t = qrcode<screen_t::control_surface_type>;
static qr_t qr_link;
static button_t qr_return;

static void switches_on_value_changed(bool value, void* state) {
    switch_t* psw = (switch_t*)state;
    const size_t index = (size_t)(psw-switches);
    printf("switch %d %s\n",(int)index,value?"on":"off");
    uint8_t payload[2];
    payload[0]=value?SET_ALARM:CLEAR_ALARM;
    payload[1]=index;
    Serial2.write((const char*)payload,sizeof(payload));
    Serial2.flush(true);
}
void setup() {
    Serial.begin(115200);
    Serial2.begin(serial_baud_rate,SERIAL_8N1,control_serial_pins.rx,control_serial_pins.tx);
    printf("Arduino version: %d.%d.%d\n",ESP_ARDUINO_VERSION_MAJOR,ESP_ARDUINO_VERSION_MINOR,ESP_ARDUINO_VERSION_PATCH);
#ifdef M5STACK_CORE2
    power.initialize(); // do this first
#endif
    spi_init();
#ifndef NO_WIFI
    bool loaded = false;
    char ssid[65];
    ssid[0]=0;
    char pass[129];
    pass[0]=0;        
    if(sd_init()) {
        puts("SD card found, looking for wifi.txt creds");
        FILE* file = fopen("/sdcard/wifi.txt","r");
        if(file!=nullptr) {
            fgets(ssid,sizeof(ssid),file);
            char* sv = strchr(ssid,'\n');
            if(sv!=nullptr) *sv='\0';
            sv = strchr(ssid,'\r');
            if(sv!=nullptr) *sv='\0';
            fgets(pass,sizeof(pass),file);
            fclose(file);
            file = nullptr;
            sv = strchr(pass,'\n');
            if(sv!=nullptr) *sv='\0';
            sv = strchr(pass,'\r');
            if(sv!=nullptr) *sv='\0';
            loaded = true;
        }
    }
    SPIFFS.begin();
    if(!loaded) {
        if(SPIFFS.exists("/wifi.txt")) {
            File file = SPIFFS.open("/wifi.txt","r");
            String str = file.readStringUntil('\n');
            if(str.endsWith("\r")) {
                str = str.substring(0,str.length()-1);
            }
            strncpy(ssid,str.c_str(),sizeof(ssid));
            str = file.readStringUntil('\n');
            file.close();
            if(str.endsWith("\r")) {
                str = str.substring(0,str.length()-1);
            }
            strncpy(pass,str.c_str(),sizeof(pass));
            loaded = true;
        }
    }
    if(loaded) {
        printf("Read wifi.txt. Connecting to %s\n", ssid);
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        WiFi.begin(ssid,pass);
    }
#endif
    lcd_init(); // do this next
#ifdef M5STACK_CORE2
    power.lcd_voltage(3.0);
#endif
    
    // init the screen and callbacks
    main_screen.dimensions({320,240});
    main_screen.buffer_size(lcd_transfer_buffer_size);
    main_screen.buffer1(lcd_transfer_buffer1);
    main_screen.buffer2(lcd_transfer_buffer2);
    main_screen.background_color(color_t::black);
    srect16 sr(0,0,main_screen.dimensions().width/2,main_screen.dimensions().width/8);
    clear_all.bounds(sr.offset(0,main_screen.dimensions().height-sr.y2-1).center_horizontal(main_screen.bounds()));
    clear_all.back_color(color32_t::dark_red);
    clear_all.color(color32_t::black);
    clear_all.border_color(color32_t::dark_gray);
    clear_all.font(font_stream);
    clear_all.font_size(sr.height()-4);
    clear_all.text("Reset all");
    clear_all.radiuses({5,5});
    clear_all.on_pressed_changed_callback([](bool pressed, void* state){
        if(!pressed) {
            for(size_t i = 0;i<switches_count;++i) {
                switches[i].value(false);
            }
        }
    });
    main_screen.register_control(clear_all);
    web_link.bounds(sr.offset(0,main_screen.dimensions().height-sr.y2-1).offset(clear_all.dimensions().width,0));
    web_link.back_color(color32_t::light_blue);
    web_link.color(color32_t::dark_blue);
    web_link.border_color(color32_t::dark_gray);
    web_link.font(font_stream);
    web_link.font_size(sr.height()-4);
    web_link.text("QR Link");
    web_link.radiuses({5,5});
    web_link.on_pressed_changed_callback([](bool pressed, void* state){
        if(!pressed) {
            lcd.active_screen(qr_screen);
        }
    });
    web_link.visible(false);
    main_screen.register_control(web_link);
    text_font = tt_font(font_stream,main_screen.dimensions().height/6,font_size_units::px);
    text_font.initialize();
    char sz[16];
    itoa(switches_count,sz,10);
    text_info ti(sz,text_font);
    size16 area;
    text_font.measure((uint16_t)-1,ti,&area);
    sr = srect16(0,0,main_screen.dimensions().width/7,main_screen.dimensions().height/3);
    const uint16_t swidth = math::max(area.width,(uint16_t)sr.width());
    const uint16_t sheight = area.height+sr.height()+2;
    const uint16_t total_width = swidth*switches_count;
    const uint16_t xofs = (main_screen.dimensions().width-total_width)/2;
    const uint16_t yofs = main_screen.dimensions().height/12;
    uint16_t x = 0;
    for(size_t i = 0;i < switches_count; ++i) {
        const uint16_t sofs = (swidth-sr.width())/2;
        switch_t& s = switches[i];
        s.bounds(srect16(x+xofs+sofs,yofs,x+xofs+sr.width()-1+sofs,yofs+sr.height()));
        s.back_color(color32_t::dark_blue);
        s.border_color(color32_t::dark_gray);
        s.knob_color(color32_t::white);
        s.knob_border_color(color32_t::dark_gray);
        s.knob_border_width(1);
        s.border_width(1);
        s.radiuses({10,10});
        s.orientation(uix_orientation::vertical);
        s.on_value_changed_callback(switches_on_value_changed,&s);
        main_screen.register_control(s);
        itoa(i+1,switch_text[i],10);
        label_t& l = switch_labels[i];
        l.text(switch_text[i]);
        l.bounds(srect16(x+xofs,yofs+sr.height()+1,x+xofs+swidth-1,yofs+sr.height()+area.height));
        l.font(text_font);
        l.color(color32_t::white);
        l.text_justify(uix_justify::top_middle);
        l.padding({0,0});
        main_screen.register_control(l);
        x+=swidth+2;
    }
    qr_screen.dimensions(main_screen.dimensions());
    sr = srect16(0,0,qr_screen.dimensions().width/2,qr_screen.dimensions().width/8);
    qr_link.bounds(srect16(0,0,qr_screen.dimensions().width/2,qr_screen.dimensions().width/2).center_horizontal(qr_screen.bounds()));
    qr_link.text("about:blank");
    qr_screen.register_control(qr_link);
    qr_return.bounds(sr.center_horizontal(qr_screen.bounds()).offset(0,qr_screen.dimensions().height-sr.height()));
    qr_return.back_color(color32_t::gray);
    qr_return.color(color32_t::white);
    qr_return.border_color(color32_t::dark_gray);
    qr_return.font(font_stream);
    qr_return.font_size(sr.height()-4);
    qr_return.text("Main screen");
    qr_return.radiuses({5,5});
    qr_return.on_pressed_changed_callback([](bool pressed, void* state){
        if(!pressed) {
            lcd.active_screen(main_screen);
        }
    });
    qr_screen.register_control(qr_return);
    lcd.active_screen(main_screen);
    while(Serial2.available()) {
        Serial2.read();
    }
}
void loop()
{
    if(Serial2.available()>=2) {
        uint8_t payload[2];
        Serial2.readBytes(payload,2);
        if(payload[0]==ALARM_THROWN) {
            const size_t index = payload[1];
            if(index<switches_count) {
                switches[index].value(true);
            }
        }
    }
#ifndef NO_WIFI
    if(!web_link.visible()) {
        if(WiFi.status()==WL_CONNECTED) {
            puts("Connected");
            www_init();
            const int16_t diff = -clear_all.bounds().x1;
            clear_all.bounds(clear_all.bounds().offset(diff,0));
            static char qr_text[256];
            strcpy(qr_text,"http://");
            strcat(qr_text,WiFi.localIP().toString().c_str());
            qr_link.text(qr_text);
            web_link.visible(true);
        }
    } else {
        if(WiFi.status()!=WL_CONNECTED) {
            if(&lcd.active_screen()!=&main_screen) {
                lcd.active_screen(main_screen);
            }
            web_link.visible(false);
            clear_all.bounds(clear_all.bounds().center_horizontal(main_screen.bounds()));
            httpd.end();
        }
    }
#endif
    lcd.update();
    touch.update();
}
