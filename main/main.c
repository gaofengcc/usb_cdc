#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "uart_learn.h"
#include "usb_cdc_bridge.h"
#include "lcd_driver.h"
#include "lcd_font.h"
#include "touch_driver.h"

static const char *TAG = "usb_cdc_main";

/** 背景色列表, 触摸后循环切换 */
static const uint16_t s_bg_colors[] = {
    LCD_COLOR_WHITE,
    LCD_COLOR_BLUE,
    LCD_COLOR_GREEN,
    LCD_COLOR_RED,
    LCD_COLOR_YELLOW,
    LCD_COLOR_CYAN,
    LCD_COLOR_MAGENTA,
};

static const size_t s_bg_color_count = sizeof(s_bg_colors) / sizeof(s_bg_colors[0]);
static size_t s_bg_color_index = 0U;

/**
 * @brief Build default UART learn engine configuration.
 *
 * @return Default UART learn configuration.
 */
static uart_learn_config_t build_uart_learn_config(void)
{
    uart_learn_config_t config = UART_LEARN_DEFAULT_CONFIG();
    uart_learn_port_mapping_t mapping = {
        .cdc_port = 0,
        .uart_port = 0,
        .tx_gpio = 45,
        .rx_gpio = 46,
        .enabled = true
    };
    config.port_mappings[0] = mapping;
    return config;
}

/**
 * @brief 按当前背景色重绘整屏内容
 */
static void lcd_demo_redraw_screen(void)
{
    uint16_t bg = s_bg_colors[s_bg_color_index];

    lcd_driver_clear(bg);
    lcd_font_draw_string_centered("HELLO WORLD", LCD_COLOR_BLACK, bg);
}

/**
 * @brief 初始化 LCD 与触摸, 并显示首屏
 *
 * @return ESP_OK 成功, 其他失败
 */
static esp_err_t lcd_demo_init(void)
{
    esp_err_t ret = lcd_driver_init(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LCD init failed");
        return ret;
    }

    ret = touch_driver_init(NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch init failed");
        return ret;
    }

    touch_driver_load_calibration();
    lcd_demo_redraw_screen();
    ESP_LOGI(TAG, "LCD demo ready");
    return ESP_OK;
}

/**
 * @brief 处理触摸: 按下时切换背景色
 */
static void lcd_demo_process_touch(void)
{
    static bool last_touched = false;
    bool touched = touch_driver_is_touched();

    if (touched && !last_touched) {
        s_bg_color_index = (s_bg_color_index + 1U) % s_bg_color_count;
        lcd_demo_redraw_screen();
        ESP_LOGI(TAG, "Background changed, index=%u", (unsigned)s_bg_color_index);
    }

    last_touched = touched;
}

/**
 * @brief Application entry.
 */
void app_main(void)
{
    // uart_learn_config_t uart_learn_config = build_uart_learn_config();

    // ESP_LOGI(TAG, "Initialize UART learn engine");
    // ESP_ERROR_CHECK(uart_learn_init(&uart_learn_config));
    // ESP_ERROR_CHECK(uart_learn_start());

    // ESP_LOGI(TAG, "Initialize USB CDC bridge");
    // ESP_ERROR_CHECK(usb_cdc_bridge_init());

    ESP_LOGI(TAG, "Initialize LCD and touch");
    ESP_ERROR_CHECK(lcd_demo_init());

    ESP_LOGI(TAG, "System initialization DONE");

    while (1) {
        usb_cdc_bridge_process();
        lcd_demo_process_touch();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
