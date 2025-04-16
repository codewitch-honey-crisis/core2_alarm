#define SPI_PORT SPI3_HOST
#define SPI_CLK 18
#define SPI_MISO 38
#define SPI_MOSI 23

#define SD_PORT SPI3_HOST
#define SD_CS 4
// UIX can draw to one buffer while sending
// another for better performance but it requires
// twice the transfer buffer memory
#define LCD_TWO_BUFFERS  // optional
// screen dimensions
#define LCD_WIDTH 320
#define LCD_HEIGHT 240
// indicates how much of the screen gets updated at once
// #define LCD_DIVISOR 2 // optional
// screen connections
#define LCD_PORT SPI3_HOST
#define LCD_DC 15
#define LCD_CS 5
#define LCD_RST -1    // optional
#define LCD_BL -1     // optional
#define LCD_BL_LOW 0  // optional
#define LCD_PANEL esp_lcd_new_panel_ili9342
#define LCD_GAP_X 0                   // optional
#define LCD_GAP_Y 0                   // optional
#define LCD_SWAP_XY 0                 // optional
#define LCD_MIRROR_X 0                // optional
#define LCD_MIRROR_Y 0                // optional
#define LCD_INVERT_COLOR 1            // optional
#define LCD_BGR 1                     // optional
#define LCD_BIT_DEPTH 16              // optional
#define LCD_SPEED (40 * 1000 * 1000)  // optional

#include <sys/stat.h>
#include <sys/unistd.h>

#include <esp_i2c.hpp>  // i2c initialization
#include <ft6336.hpp>
#include <gfx.hpp>            // graphics library
#include <m5core2_power.hpp>  // AXP192 power management (core2)
#include <uix.hpp>            // user interface library

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_http_server.h"
#include "esp_lcd_panel_ili9342.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

// font is a TTF/OTF from downloaded from fontsquirrel.com
// converted to a header with https://honeythecodewitch.com/gfx/converter
#define OPENSANS_REGULAR_IMPLEMENTATION
#include "assets/OpenSans_Regular.h"  // our font
#define LEFT_ARROW_IMPLEMENTATION
#include "assets/left_arrow.h"
#define RIGHT_ARROW_IMPLEMENTATION
#include "assets/right_arrow.h"
#include "config.h"

// namespace imports
using namespace esp_idf;  // devices
using namespace gfx;      // graphics
using namespace uix;      // user interface

using color_t = color<rgb_pixel<16>>;     // screen color
using color32_t = color<rgba_pixel<32>>;  // UIX color

// fonts load from streams, so wrap our array in one
static const_buffer_stream font_stream(OpenSans_Regular,
                                       sizeof(OpenSans_Regular));
static tt_font text_font;

static const_buffer_stream left_stream(left_arrow, sizeof(left_arrow));
static const_buffer_stream right_stream(right_arrow, sizeof(right_arrow));

using screen_t = uix::screen<rgb_pixel<LCD_BIT_DEPTH>>;
using surface_t = screen_t::control_surface_type;

template <typename ControlSurfaceType>
class arrow_box : public control<ControlSurfaceType> {
    using base_type = control<ControlSurfaceType>;

   public:
    typedef void (*on_pressed_changed_callback_type)(bool pressed, void* state);

   private:
    bool m_pressed;
    bool m_dirty;
    sizef m_svg_size;
    matrix m_fit;
    on_pressed_changed_callback_type m_on_pressed_changed_callback;
    void* m_on_pressed_changed_callback_state;
    stream* m_svg;
    static rectf correct_aspect(srect16& sr, float aspect) {
        if (sr.width() > sr.height()) {
            sr.y2 /= aspect;
        } else {
            sr.x2 *= aspect;
        }
        return (rectf)sr;
    }

   public:
    arrow_box()
        : base_type(),
          m_pressed(false),
          m_dirty(true),
          m_on_pressed_changed_callback(nullptr),
          m_svg(nullptr) {}
    void svg(stream& svg_stream) {
        m_svg = &svg_stream;
        m_dirty = true;
        this->invalidate();
    }
    stream& svg() const { return *m_svg; }
    bool pressed() const { return m_pressed; }
    void on_pressed_changed_callback(on_pressed_changed_callback_type callback,
                                     void* state = nullptr) {
        m_on_pressed_changed_callback = callback;
        m_on_pressed_changed_callback_state = state;
    }

   protected:
    virtual void on_before_paint() override {
        if (m_dirty) {
            m_svg_size = {0.f, 0.f};
            if (m_svg != nullptr) {
                m_svg->seek(0);
                canvas::svg_dimensions(*m_svg, &m_svg_size);
                srect16 sr = this->dimensions().bounds();
                ssize16 dim = this->dimensions();
                const float xo = dim.width / 8;
                const float yo = dim.height / 8;
                rectf corrected = correct_aspect(sr, m_svg_size.aspect_ratio())
                                      .inflate(-xo, -yo);
                ;
                m_fit = matrix::create_fit_to(
                    m_svg_size,
                    corrected.offset((dim.width - corrected.width()) * .5f +
                                         (xo * m_pressed),
                                     (dim.height - corrected.height()) * .5f +
                                         (yo * m_pressed)));
            }
            m_dirty = false;
        }
    }
    virtual void on_paint(ControlSurfaceType& dst,
                          const srect16& clip) override {
        if (m_dirty || m_svg == nullptr) {
            puts("Paint not ready");
            return;
        }
        canvas cvs((size16)this->dimensions());
        cvs.initialize();
        draw::canvas(dst, cvs);
        m_svg->seek(0);
        if (gfx_result::success != cvs.render_svg(*m_svg, m_fit)) {
            puts("SVG render error");
        }
    }
    virtual bool on_touch(size_t locations_size,
                          const spoint16* locations) override {
        if (!m_pressed) {
            m_pressed = true;
            if (m_on_pressed_changed_callback != nullptr) {
                m_on_pressed_changed_callback(
                    true, m_on_pressed_changed_callback_state);
            }
            m_dirty = true;
            this->invalidate();
        }
        return true;
    }
    virtual void on_release() override {
        if (m_pressed) {
            m_pressed = false;
            if (m_on_pressed_changed_callback != nullptr) {
                m_on_pressed_changed_callback(
                    false, m_on_pressed_changed_callback_state);
            }
            m_dirty = true;
            this->invalidate();
        }
    }
};

static void update_switches(bool lock = true);
static void serial_send_alarm(size_t i);

static bool alarm_values[alarm_count];

static void alarm_enable(size_t alarm, bool on) {
    if (alarm < 0 || alarm >= alarm_count) return;
    if (alarm_values[alarm] != on) {
        alarm_values[alarm] = on;
        serial_send_alarm(alarm);
    }
}

static uix::display lcd;

static constexpr const EventBits_t wifi_connected_bit = BIT0;
static constexpr const EventBits_t wifi_fail_bit = BIT1;
static EventGroupHandle_t wifi_event_group = NULL;
static char wifi_ssid[65];
static char wifi_pass[129];
static esp_ip4_addr_t wifi_ip;

static void serial_init() {
    uart_config_t uart_config;
    memset(&uart_config, 0, sizeof(uart_config));
    uart_config.baud_rate = serial_baud_rate;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, 256, 0, 20, nullptr, 0));
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, control_serial_pins.tx, control_serial_pins.rx,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}
struct serial_event {
    COMMAND_ID cmd;
    uint8_t arg;
};
static bool serial_get_event(serial_event* out_event) {
    uint8_t payload[2];
    if (out_event && sizeof(payload) == uart_read_bytes(UART_NUM_1, &payload,
                                                        sizeof(payload), 0)) {
        out_event->cmd = (COMMAND_ID)payload[0];
        out_event->arg = payload[1];
        return true;
    }
    return false;
}
static void serial_send_alarm(size_t i) {
    if (i >= alarm_count) return;
    printf("%s alarm #%d\n", alarm_values[i] ? "setting" : "clearing",
           (int)i + 1);
    uint8_t payload[2];
    payload[0] = alarm_values[i] ? SET_ALARM : CLEAR_ALARM;
    payload[1] = i;
    uart_write_bytes(UART_NUM_1, payload, sizeof(payload));
}

static size_t wifi_retry_count = 0;
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_retry_count < 3) {
            esp_wifi_connect();
            ++wifi_retry_count;
        } else {
            puts("wifi connection failed");
            xEventGroupSetBits(wifi_event_group, wifi_fail_bit);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        puts("got IP address");
        wifi_retry_count = 0;
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        memcpy(&wifi_ip, &event->ip_info.ip, sizeof(wifi_ip));
        xEventGroupSetBits(wifi_event_group, wifi_connected_bit);
    }
}
static bool wifi_load(const char* path, char* ssid, char* pass) {
    FILE* file = fopen(path, "r");
    if (file != nullptr) {
        // parse the file
        fgets(ssid, 64, file);
        char* sv = strchr(ssid, '\n');
        if (sv != nullptr) *sv = '\0';
        sv = strchr(ssid, '\r');
        if (sv != nullptr) *sv = '\0';
        fgets(pass, 128, file);
        fclose(file);
        sv = strchr(pass, '\n');
        if (sv != nullptr) *sv = '\0';
        sv = strchr(pass, '\r');
        if (sv != nullptr) *sv = '\0';
        return true;
    }
    return false;
}
static void wifi_init(const char* ssid, const char* password) {
    nvs_flash_init();
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
        &instance_got_ip));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    memcpy(wifi_config.sta.ssid, ssid, strlen(ssid) + 1);
    memcpy(wifi_config.sta.password, password, strlen(password) + 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    // wifi_config.sta.sae_h2e_identifier[0]=0;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}
enum WIFI_STATUS { WIFI_WAITING, WIFI_CONNECTED, WIFI_CONNECT_FAILED };
static WIFI_STATUS wifi_status() {
    if (wifi_event_group == nullptr) {
        return WIFI_WAITING;
    }
    EventBits_t bits = xEventGroupGetBits(wifi_event_group) &
                       (wifi_connected_bit | wifi_fail_bit);
    if (bits == wifi_connected_bit) {
        return WIFI_CONNECTED;
    } else if (bits == wifi_fail_bit) {
        return WIFI_CONNECT_FAILED;
    }
    return WIFI_WAITING;
}

static httpd_handle_t httpd_handle = nullptr;
static SemaphoreHandle_t httpd_ui_sync = nullptr;
struct httpd_async_resp_arg {
    httpd_handle_t hd;
    int fd;
};
static void httpd_send_chunked(httpd_async_resp_arg* resp_arg,
                               const char* buffer, size_t buffer_len) {
    char buf[64];
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
    itoa(buffer_len, buf, 16);
    strcat(buf, "\r\n");
    httpd_socket_send(hd, fd, buf, strlen(buf), 0);
    if (buffer && buffer_len) {
        httpd_socket_send(hd, fd, buffer, buffer_len, 0);
    }
    httpd_socket_send(hd, fd, "\r\n", 2, 0);
}
static const char* httpd_crack_query(const char* url_part, char* name,
                                     char* value) {
    if (url_part == nullptr || !*url_part) return nullptr;
    const char start = *url_part;
    if (start == '&' || start == '?') {
        ++url_part;
    }
    size_t i = 0;
    char* name_cur = name;
    while (*url_part && *url_part != '=') {
        if (i < 64) {
            *name_cur++ = *url_part;
        }
        ++url_part;
        ++i;
    }
    *name_cur = '\0';
    if (!*url_part) {
        *value = '\0';
        return url_part;
    }
    ++url_part;
    i = 0;
    char* value_cur = value;
    while (*url_part && *url_part != '&' && i < 64) {
        *value_cur++ = *url_part++;
        ++i;
    }
    *value_cur = '\0';
    return url_part;
}
static void httpd_parse_url_and_apply_alarms(const char* url) {
    const char* query = strchr(url, '?');
    bool has_set = false;
    char name[64];
    char value[64];
    bool req_values[alarm_count];
    if (query != nullptr) {
        memset(req_values, 0, sizeof(req_values));
        while (1) {
            query = httpd_crack_query(query, name, value);
            if (!query) {
                break;
            }
            if (!strcmp("set", name)) {
                has_set = true;
            } else if (!strcmp("a", name)) {
                char* endsz;
                long l = strtol(value, &endsz, 10);
                if (l >= 0 && l < alarm_count) {
                    req_values[l] = true;
                }
            }
        }
    }
    if (has_set) {
        for (size_t i = 0; i < alarm_count; ++i) {
            alarm_enable(i, req_values[i]);
        }
        update_switches();
    }
}
static void httpd_send_block(const char* data, size_t len, void* arg) {
    if (!data || !*data || !len) {
        return;
    }
    httpd_async_resp_arg* resp_arg = (httpd_async_resp_arg*)arg;
    httpd_socket_send(resp_arg->hd, resp_arg->fd, data, len, 0);
}
static void httpd_send_expr(int expr, void* arg) {
    httpd_async_resp_arg* resp_arg = (httpd_async_resp_arg*)arg;
    char buf[64];
    itoa(expr, buf, 10);
    httpd_send_chunked(resp_arg, buf, strlen(buf));
}
static void httpd_send_expr(const char* expr, void* arg) {
    httpd_async_resp_arg* resp_arg = (httpd_async_resp_arg*)arg;
    if (!expr || !*expr) {
        return;
    }
    httpd_send_chunked(resp_arg, expr, strlen(expr));
}
static void httpd_page_async_handler(void* resp_arg) {
    // generated from clasp/page.clasp:
    #include "httpd_page.h"
    free(resp_arg);
}
static void httpd_api_async_handler(void* resp_arg) {
    // generated from clasp/api.clasp:
    #include "httpd_api.h"
    free(resp_arg);
}
static esp_err_t httpd_request_handler(httpd_req_t* req) {
    httpd_async_resp_arg* resp_arg =
        (httpd_async_resp_arg*)malloc(sizeof(httpd_async_resp_arg));
    if (resp_arg == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    httpd_parse_url_and_apply_alarms(req->uri);
    resp_arg->hd = req->handle;
    resp_arg->fd = httpd_req_to_sockfd(req);
    if (resp_arg->fd < 0) {
        return ESP_FAIL;
    }
    httpd_queue_work(req->handle, (httpd_work_fn_t)req->user_ctx, resp_arg);
    return ESP_OK;
}
static void httpd_init() {
    httpd_ui_sync = xSemaphoreCreateMutex();
    if (httpd_ui_sync == nullptr) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    /* Modify this setting to match the number of test URI handlers */
    config.max_uri_handlers = 2;
    config.server_port = 80;
    config.max_open_sockets = (CONFIG_LWIP_MAX_SOCKETS - 3);
    ESP_ERROR_CHECK(httpd_start(&httpd_handle, &config));
    httpd_uri_t handler = {.uri = "/",
                           .method = HTTP_GET,
                           .handler = httpd_request_handler,
                           .user_ctx = (void*)httpd_page_async_handler};
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpd_handle, &handler));
    handler = {.uri = "/api",
               .method = HTTP_GET,
               .handler = httpd_request_handler,
               .user_ctx = (void*)httpd_api_async_handler};
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpd_handle, &handler));
}
static void httpd_end() {
    if (httpd_handle == nullptr) {
        return;
    }
    ESP_ERROR_CHECK(httpd_stop(httpd_handle));
    httpd_handle = nullptr;
    vSemaphoreDelete(httpd_ui_sync);
    httpd_ui_sync = nullptr;
}

static void power_init() {
    // for AXP192 power management
    static m5core2_power power(esp_i2c<1, 21, 22>::instance);
    // draw a little less power
    power.initialize();
    power.lcd_voltage(3.0);
}

static void spi_init() {
    spi_bus_config_t buscfg;
    memset(&buscfg, 0, sizeof(buscfg));
    buscfg.sclk_io_num = SPI_CLK;
    buscfg.mosi_io_num = SPI_MOSI;
    buscfg.miso_io_num = SPI_MISO;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
#ifdef LCD_DIVISOR
    static constexpr const size_t lcd_divisor = LCD_DIVISOR;
#else
    static constexpr const size_t lcd_divisor = 10;
#endif
#ifdef LCD_BIT_DEPTH
    static constexpr const size_t lcd_pixel_size = (LCD_BIT_DEPTH + 7) / 8;
#else
    static constexpr const size_t lcd_pixel_size = 2;
#endif
    // the size of our transfer buffer(s)
    static const constexpr size_t lcd_transfer_buffer_size =
        LCD_WIDTH * LCD_HEIGHT * lcd_pixel_size / lcd_divisor;

    buscfg.max_transfer_sz =
        (lcd_transfer_buffer_size > 512 ? lcd_transfer_buffer_size : 512) + 8;
    // Initialize the SPI bus on VSPI (SPI3)
    spi_bus_initialize(SPI_PORT, &buscfg, SPI_DMA_CH_AUTO);
}

// initialize the screen using the esp panel API
static void lcd_init() {
    // for the touch panel
    using touch_t = ft6336<320, 280, 16>;
    static touch_t touch(esp_i2c<1, 21, 22>::instance);

#ifdef LCD_DIVISOR
    static constexpr const size_t lcd_divisor = LCD_DIVISOR;
#else
    static constexpr const size_t lcd_divisor = 10;
#endif
#ifdef LCD_BIT_DEPTH
    static constexpr const size_t lcd_pixel_size = (LCD_BIT_DEPTH + 7) / 8;
#else
    static constexpr const size_t lcd_pixel_size = 2;
#endif
    // the size of our transfer buffer(s)
    static const constexpr size_t lcd_transfer_buffer_size =
        LCD_WIDTH * LCD_HEIGHT * lcd_pixel_size / lcd_divisor;

    uint8_t* lcd_transfer_buffer1 =
        (uint8_t*)heap_caps_malloc(lcd_transfer_buffer_size, MALLOC_CAP_DMA);
    uint8_t* lcd_transfer_buffer2 =
        (uint8_t*)heap_caps_malloc(lcd_transfer_buffer_size, MALLOC_CAP_DMA);
    if (lcd_transfer_buffer1 == nullptr || lcd_transfer_buffer2 == nullptr) {
        puts("Out of memory allocating transfer buffers");
        while (1) vTaskDelay(5);
    }
#if defined(LCD_BL) && LCD_BL > 1
#ifdef LCD_BL_LOW
    static constexpr const int bl_on = !(LCD_BL_LOW);
#else
    static constexpr const int bl_on = 1;
#endif
    gpio_set_direction((gpio_num_t)LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)LCD_BL, !bl_on);
#endif
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config;
    memset(&io_config, 0, sizeof(io_config));
    io_config.dc_gpio_num = LCD_DC;
    io_config.cs_gpio_num = LCD_CS;
#ifdef LCD_SPEED
    io_config.pclk_hz = LCD_SPEED;
#else
    io_config.pclk_hz = 20 * 1000 * 1000;
#endif
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 10;
    io_config.on_color_trans_done = [](esp_lcd_panel_io_handle_t lcd_io,
                                       esp_lcd_panel_io_event_data_t* edata,
                                       void* user_ctx) {
        lcd.flush_complete();
        return true;
    };
    // Attach the LCD to the SPI bus
    esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_PORT, &io_config,
                             &io_handle);

    esp_lcd_panel_handle_t lcd_handle = NULL;
    esp_lcd_panel_dev_config_t lcd_config;
    memset(&lcd_config, 0, sizeof(lcd_config));
#ifdef LCD_RST
    lcd_config.reset_gpio_num = LCD_RST;
#else
    lcd_config.reset_gpio_num = -1;
#endif
#if defined(LCD_BGR) && LCD_BGR != 0
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    lcd_config.rgb_endian = LCD_RGB_ENDIAN_BGR;
#else
    lcd_config.color_space = ESP_LCD_COLOR_SPACE_BGR;
#endif
#else
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    lcd_config.rgb_endian = LCD_RGB_ENDIAN_RGB;
#else
    lcd_config.color_space = ESP_LCD_COLOR_SPACE_RGB;
#endif
#endif
#ifdef LCD_BIT_DEPTH
    lcd_config.bits_per_pixel = LCD_BIT_DEPTH;
#else
    lcd_config.bits_per_pixel = 16;
#endif

    // Initialize the LCD configuration
    LCD_PANEL(io_handle, &lcd_config, &lcd_handle);

    // Reset the display
    esp_lcd_panel_reset(lcd_handle);

    // Initialize LCD panel
    esp_lcd_panel_init(lcd_handle);
#ifdef LCD_GAP_X
    static constexpr int lcd_gap_x = LCD_GAP_X;
#else
    static constexpr int lcd_gap_x = 0;
#endif
#ifdef LCD_GAP_Y
    static constexpr int lcd_gap_y = LCD_GAP_Y;
#else
    static constexpr int lcd_gap_y = 0;
#endif
    esp_lcd_panel_set_gap(lcd_handle, lcd_gap_x, lcd_gap_y);
#ifdef LCD_SWAP_XY
    esp_lcd_panel_swap_xy(lcd_handle, LCD_SWAP_XY);
#endif
#ifdef LCD_MIRROR_X
    static constexpr int lcd_mirror_x = LCD_MIRROR_X;
#else
    static constexpr int lcd_mirror_x = 0;
#endif
#ifdef LCD_MIRROR_Y
    static constexpr int lcd_mirror_y = LCD_MIRROR_Y;
#else
    static constexpr int lcd_mirror_y = 0;
#endif
    esp_lcd_panel_mirror(lcd_handle, lcd_mirror_x, lcd_mirror_y);
#ifdef LCD_INVERT_COLOR
    esp_lcd_panel_invert_color(lcd_handle, LCD_INVERT_COLOR);
#endif

    // Turn on the screen
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    esp_lcd_panel_disp_on_off(lcd_handle, true);
#else
    esp_lcd_panel_disp_off(lcd_handle, false);
#endif
#if defined(LCD_BL) && LCD_BL > 1
    gpio_set_level((gpio_num_t)LCD_BL, bl_on);
#endif
    lcd.buffer_size(lcd_transfer_buffer_size);
    lcd.buffer1(lcd_transfer_buffer1);
    lcd.buffer2(lcd_transfer_buffer2);
    lcd.on_flush_callback(
        [](const rect16& bounds, const void* bmp, void* state) {
            int x1 = bounds.x1, y1 = bounds.y1, x2 = bounds.x2 + 1,
                y2 = bounds.y2 + 1;
            esp_lcd_panel_draw_bitmap((esp_lcd_panel_handle_t)state, x1, y1, x2,
                                      y2, (void*)bmp);
        },
        lcd_handle);
    lcd.on_touch_callback(
        [](point16* out_locations, size_t* in_out_locations_size, void* state) {
            touch.update();
            // UIX supports multiple touch points.
            // so does the FT6336 so we potentially have
            // two values
            *in_out_locations_size = 0;
            uint16_t x, y;
            if (touch.xy(&x, &y)) {
                out_locations[0] = point16(x, y);
                ++*in_out_locations_size;
                if (touch.xy2(&x, &y)) {
                    out_locations[1] = point16(x, y);
                    ++*in_out_locations_size;
                }
            }
        });
    touch.initialize();
    touch.rotation(0);
}

static sdmmc_card_t* sd_card = nullptr;
static bool sd_init() {
    static const char mount_point[] = "/sdcard";
    esp_vfs_fat_sdmmc_mount_config_t mount_config;
    memset(&mount_config, 0, sizeof(mount_config));
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 0;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_PORT;
    // // This initializes the slot without card detect (CD) and write
    // protect (WP)
    // // signals.
    sdspi_device_config_t slot_config;
    memset(&slot_config, 0, sizeof(slot_config));
    slot_config.host_id = (spi_host_device_t)SD_PORT;
    slot_config.gpio_cs = (gpio_num_t)SD_CS;
    slot_config.gpio_cd = SDSPI_SLOT_NO_CD;
    slot_config.gpio_wp = SDSPI_SLOT_NO_WP;
    slot_config.gpio_int = GPIO_NUM_NC;
    if (ESP_OK != esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config,
                                          &mount_config, &sd_card)) {
        return false;
    }
    return true;
}

static void spiffs_init() {
    esp_vfs_spiffs_conf_t conf;
    memset(&conf, 0, sizeof(conf));
    conf.base_path = "/spiffs";
    conf.partition_label = NULL;
    conf.max_files = 5;
    conf.format_if_mount_failed = true;
    if (ESP_OK != esp_vfs_spiffs_register(&conf)) {
        puts("Unable to initialize SPIFFS");
        while (1) vTaskDelay(5);
    }
}

using button_t = vbutton<surface_t>;
using switch_t = vswitch<surface_t>;
using label_t = label<surface_t>;
using qr_t = qrcode<surface_t>;
using arrow_t = arrow_box<surface_t>;

static screen_t main_screen;
static arrow_t left_button;
static arrow_t right_button;
static button_t reset_all;
static button_t web_link;
static constexpr const size_t switches_count =
    math::min(alarm_count, (size_t)(LCD_WIDTH / 40) / 2);
static switch_t switches[switches_count];
static label_t switch_labels[switches_count];
static char switch_text[switches_count][6];
static size_t switch_index = 0;
static bool switches_updating = false;
static screen_t qr_screen;
static qr_t qr_link;
static button_t qr_return;

static void update_switches(bool lock) {
    if (lock && httpd_ui_sync != nullptr) {
        xSemaphoreTake(httpd_ui_sync, portMAX_DELAY);
    }
    switches_updating = true;
    for (size_t i = 0; i < switches_count; ++i) {
        itoa(1 + i + switch_index, switch_text[i], 10);
        switch_labels[i].text(switch_text[i]);
        switches[i].value(alarm_values[i + switch_index]);
    }
    left_button.visible(switch_index != 0);
    right_button.visible(switch_index < alarm_count - switches_count);
    switches_updating = false;
    if (lock && httpd_ui_sync != nullptr) {
        xSemaphoreGive(httpd_ui_sync);
    }
}

static void loop();
static void loop_task(void* arg) {
    uint32_t ts = pdTICKS_TO_MS(xTaskGetTickCount());
    while (1) {
        loop();
        uint32_t ms = pdTICKS_TO_MS(xTaskGetTickCount());
        if (ms > ts + 200) {
            ms = pdTICKS_TO_MS(xTaskGetTickCount());
            vTaskDelay(5);
        }
    }
}
extern "C" void app_main() {
    printf("ESP-IDF version: %d.%d.%d\n", ESP_IDF_VERSION_MAJOR,
           ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
    power_init();  // do this first
    spi_init();    // used by the LCD and SD reader
    // initialize the display
    lcd_init();
    spiffs_init();
    memset(alarm_values, 0, sizeof(alarm_values));
    serial_init();
    bool loaded = false;

    wifi_ssid[0] = 0;

    wifi_pass[0] = 0;
    if (sd_init()) {
        puts("SD card found, looking for wifi.txt creds");
        loaded = wifi_load("/sdcard/wifi.txt", wifi_ssid, wifi_pass);
    }
    if (!loaded) {
        puts("Looking for wifi.txt creds on internal flash");
        loaded = wifi_load("/spiffs/wifi.txt", wifi_ssid, wifi_pass);
    }
    if (loaded) {
        printf("Initializing WiFi connection to %s\n", wifi_ssid);
        wifi_init(wifi_ssid, wifi_pass);
    }
    main_screen.dimensions({LCD_WIDTH, LCD_HEIGHT});
    main_screen.background_color(color_t::black);

    left_button.bounds(srect16(0, 0, LCD_WIDTH / 8.77f, LCD_WIDTH / 8)
                           .center_vertical(main_screen.bounds())
                           .offset(LCD_WIDTH / 53, 0));
    left_button.svg(left_stream);
    left_button.on_pressed_changed_callback([](bool pressed, void* state) {
        if (pressed) {
            if (switch_index > 0) {
                --switch_index;
                // we're already locking from just outside lcd.update()
                update_switches(false);
            }
        }
    });
    left_button.visible(false);
    main_screen.register_control(left_button);
    right_button.bounds(srect16(0, 0, LCD_WIDTH / 8.77f, LCD_WIDTH / 8)
                            .center_vertical(main_screen.bounds())
                            .offset(main_screen.bounds().x2 -
                                        left_button.dimensions().width -
                                        LCD_WIDTH / 53,
                                    0));
    right_button.svg(right_stream);
    right_button.visible(alarm_count > switches_count);
    right_button.on_pressed_changed_callback([](bool pressed, void* state) {
        if (pressed) {
            if (switch_index < alarm_count - switches_count) {
                ++switch_index;
                // we're already locking from just outside lcd.update()
                update_switches(false);
            }
        }
    });
    main_screen.register_control(right_button);

    srect16 sr(0, 0, main_screen.dimensions().width / 2,
               main_screen.dimensions().width / 8);
    reset_all.bounds(sr.offset(0, main_screen.dimensions().height - sr.y2 - 1)
                         .center_horizontal(main_screen.bounds()));
    reset_all.back_color(color32_t::dark_red);
    reset_all.color(color32_t::black);
    reset_all.border_color(color32_t::dark_gray);
    reset_all.font(font_stream);
    reset_all.font_size(sr.height() - 4);
    reset_all.text("Reset all");
    reset_all.radiuses({5, 5});
    reset_all.on_pressed_changed_callback([](bool pressed, void* state) {
        if (pressed) {
            for (size_t i = 0; i < alarm_count; ++i) {
                alarm_enable(i, false);
            }
            switches_updating = true;
            for (size_t i = 0; i < switches_count; ++i) {
                switches[i].value(false);
            }
            switches_updating = false;
        }
    });
    main_screen.register_control(reset_all);
    web_link.bounds(sr.offset(0, main_screen.dimensions().height - sr.y2 - 1)
                        .offset(reset_all.dimensions().width, 0));
    web_link.back_color(color32_t::light_blue);
    web_link.color(color32_t::dark_blue);
    web_link.border_color(color32_t::dark_gray);
    web_link.font(font_stream);
    web_link.font_size(sr.height() - 4);
    web_link.text("QR Link");
    web_link.radiuses({5, 5});
    web_link.on_pressed_changed_callback([](bool pressed, void* state) {
        if (!pressed) {
            lcd.active_screen(qr_screen);
        }
    });
    web_link.visible(false);
    main_screen.register_control(web_link);

    text_font = tt_font(font_stream, main_screen.dimensions().height / 6,
                        font_size_units::px);
    text_font.initialize();
    char sz[16];
    itoa(alarm_count, sz, 10);
    text_info ti(sz, text_font);
    size16 area;
    // measure the size of the largest number and set all the text labels to
    // that width:
    text_font.measure((uint16_t)-1, ti, &area);
    sr = srect16(0, 0, main_screen.dimensions().width / 8,
                 main_screen.dimensions().height / 3);
    const uint16_t swidth = math::max(area.width, (uint16_t)sr.width());
    const uint16_t total_width = swidth * switches_count;
    const uint16_t xofs = (main_screen.dimensions().width - total_width) / 2;
    const uint16_t yofs = main_screen.dimensions().height / 12;
    uint16_t x = 0;
    // init the fire switch controls + labels
    for (size_t i = 0; i < switches_count; ++i) {
        const uint16_t sofs = (swidth - sr.width()) / 2;
        switch_t& s = switches[i];
        s.bounds(srect16(x + xofs + sofs, yofs,
                         x + xofs + sr.width() - 1 + sofs, yofs + sr.height()));
        s.back_color(color32_t::dark_blue);
        s.border_color(color32_t::dark_gray);
        s.knob_color(color32_t::white);
        s.knob_border_color(color32_t::dark_gray);
        s.knob_border_width(1);
        s.border_width(1);
        s.radiuses({10, 10});
        s.orientation(uix_orientation::vertical);
        s.on_value_changed_callback(
            [](bool value, void* state) {
                if (!switches_updating) {
                    switch_t* psw = (switch_t*)state;
                    const size_t i = (size_t)(psw - switches) + switch_index;
                    alarm_enable(i, value);
                }
            },
            &s);
        main_screen.register_control(s);
        itoa(i + 1, switch_text[i], 10);
        label_t& l = switch_labels[i];
        l.text(switch_text[i]);
        l.bounds(srect16(x + xofs, yofs + sr.height() + 1,
                         x + xofs + swidth - 1,
                         yofs + sr.height() + area.height));
        l.font(text_font);
        l.color(color32_t::white);
        l.text_justify(uix_justify::top_middle);
        l.padding({0, 0});
        main_screen.register_control(l);
        x += swidth + 2;
    }
    // initialize the QR screen
    qr_screen.dimensions(main_screen.dimensions());
    // initialize the controls
    sr = srect16(0, 0, qr_screen.dimensions().width / 2,
                 qr_screen.dimensions().width / 8);
    qr_link.bounds(srect16(0, 0, qr_screen.dimensions().width / 2,
                           qr_screen.dimensions().width / 2)
                       .center_horizontal(qr_screen.bounds()));
    qr_link.text("about:blank");
    qr_screen.register_control(qr_link);
    qr_return.bounds(
        sr.center_horizontal(qr_screen.bounds())
            .offset(0, qr_screen.dimensions().height - sr.height()));
    qr_return.back_color(color32_t::gray);
    qr_return.color(color32_t::white);
    qr_return.border_color(color32_t::dark_gray);
    qr_return.font(font_stream);
    qr_return.font_size(sr.height() - 4);
    qr_return.text("Main screen");
    qr_return.radiuses({5, 5});
    qr_return.on_pressed_changed_callback([](bool pressed, void* state) {
        if (!pressed) {
            lcd.active_screen(main_screen);
        }
    });
    qr_screen.register_control(qr_return);
    // set the display to our main screen
    lcd.active_screen(main_screen);
    TaskHandle_t loop_handle;
    xTaskCreate(loop_task, "loop_task", 4096, nullptr, 10, &loop_handle);
    printf("Free SRAM: %0.2fKB\n", esp_get_free_internal_heap_size() / 1024.f);
}
static void loop() {
    // update the display and touch device
    if (httpd_ui_sync != nullptr) {
        xSemaphoreTake(httpd_ui_sync, portMAX_DELAY);
    }
    lcd.update();
    if (httpd_ui_sync != nullptr) {
        xSemaphoreGive(httpd_ui_sync);
    }
    serial_event evt;
    if (serial_get_event(&evt)) {
        switch (evt.cmd) {
            case ALARM_THROWN:
                alarm_enable(evt.arg, true);
                break;
            default:
                puts("Unknown event received");
                break;
        }
    }
    if (!web_link.visible()) {  // not connected yet
        if (wifi_status() == WIFI_CONNECTED) {
            puts("Connected");
            // initialize the web server
            puts("Starting web server");
            httpd_init();
            // move the "Reset all" button to the left
            const int16_t diff = -reset_all.bounds().x1;
            reset_all.bounds(reset_all.bounds().offset(diff, 0));
            // set the QR text to our website
            static char qr_text[256];
            snprintf(qr_text, sizeof(qr_text), "http://" IPSTR,
                     IP2STR(&wifi_ip));
            qr_link.text(qr_text);
            // now show the link
            web_link.visible(true);
            printf("Free SRAM: %0.2fKB\n",
                   esp_get_free_internal_heap_size() / 1024.f);
        }
    } else {
        if (wifi_status() == WIFI_CONNECT_FAILED) {
            // we disconnected for some reason
            // if it's not the main screen, set it to the main screen
            if (&lcd.active_screen() != &main_screen) {
                lcd.active_screen(main_screen);
            }
            // hide the QR Link button
            web_link.visible(false);
            // center the "Reset all" button
            reset_all.bounds(
                reset_all.bounds().center_horizontal(main_screen.bounds()));
            httpd_end();
            wifi_retry_count = 0;
            esp_wifi_start();
            printf("Free SRAM: %0.2fKB\n",
                   esp_get_free_internal_heap_size() / 1024.f);
        }
    }
}
