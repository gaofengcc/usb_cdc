/*
 * SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdint.h>
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_cdc_acm.h"
#include "tusb.h"
#include "sdkconfig.h"
#include "uart_learn.h"
#include "usb_cdc_bridge.h"

static const char *TAG = "usb_cdc_bridge";
static uint8_t rx_buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];

#define CDC_PORT_COUNT              4U
#define CDC_INTERFACE_PER_PORT      2U
#define CDC_TOTAL_INTERFACE_COUNT   (CDC_PORT_COUNT * CDC_INTERFACE_PER_PORT)
#define CDC_FS_CONFIG_TOTAL_LEN     (TUD_CONFIG_DESC_LEN + CDC_PORT_COUNT * (TUD_CDC_DESC_LEN - 7))
#define CDC_BULK_EP_SIZE            64U

#define UART_BRIDGE_BAUDRATE        115200
#define UART_BRIDGE_RX_BUF_SIZE     256U
#define UART_BRIDGE_TX_BUF_SIZE     256U
#define UART1_TX_GPIO               17
#define UART1_RX_GPIO               18
#define UART2_TX_GPIO               15
#define UART2_RX_GPIO               16

enum {
    ITF_NUM_CDC0 = 0,
    ITF_NUM_CDC0_DATA,
    ITF_NUM_CDC1,
    ITF_NUM_CDC1_DATA,
    ITF_NUM_CDC2,
    ITF_NUM_CDC2_DATA,
    ITF_NUM_CDC3,
    ITF_NUM_CDC3_DATA,
};

static const uint8_t g_cdc4_no_notify_fs_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, CDC_TOTAL_INTERFACE_COUNT, 0, CDC_FS_CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // CDC0: no interrupt notification endpoint, only bulk IN/OUT.
    8, TUSB_DESC_INTERFACE_ASSOCIATION, ITF_NUM_CDC0, 2, TUSB_CLASS_CDC, CDC_COMM_SUBCLASS_ABSTRACT_CONTROL_MODEL, CDC_COMM_PROTOCOL_NONE, 0,
    9, TUSB_DESC_INTERFACE, ITF_NUM_CDC0, 0, 0, TUSB_CLASS_CDC, CDC_COMM_SUBCLASS_ABSTRACT_CONTROL_MODEL, CDC_COMM_PROTOCOL_NONE, 0,
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_HEADER, U16_TO_U8S_LE(0x0120),
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_CALL_MANAGEMENT, 0, ITF_NUM_CDC0_DATA,
    4, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_ABSTRACT_CONTROL_MANAGEMENT, 6,
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_UNION, ITF_NUM_CDC0, ITF_NUM_CDC0_DATA,
    9, TUSB_DESC_INTERFACE, ITF_NUM_CDC0_DATA, 0, 2, TUSB_CLASS_CDC_DATA, 0, 0, 0,
    7, TUSB_DESC_ENDPOINT, 0x01, TUSB_XFER_BULK, U16_TO_U8S_LE(CDC_BULK_EP_SIZE), 0,
    7, TUSB_DESC_ENDPOINT, 0x81, TUSB_XFER_BULK, U16_TO_U8S_LE(CDC_BULK_EP_SIZE), 0,

    // CDC1
    8, TUSB_DESC_INTERFACE_ASSOCIATION, ITF_NUM_CDC1, 2, TUSB_CLASS_CDC, CDC_COMM_SUBCLASS_ABSTRACT_CONTROL_MODEL, CDC_COMM_PROTOCOL_NONE, 0,
    9, TUSB_DESC_INTERFACE, ITF_NUM_CDC1, 0, 0, TUSB_CLASS_CDC, CDC_COMM_SUBCLASS_ABSTRACT_CONTROL_MODEL, CDC_COMM_PROTOCOL_NONE, 0,
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_HEADER, U16_TO_U8S_LE(0x0120),
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_CALL_MANAGEMENT, 0, ITF_NUM_CDC1_DATA,
    4, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_ABSTRACT_CONTROL_MANAGEMENT, 6,
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_UNION, ITF_NUM_CDC1, ITF_NUM_CDC1_DATA,
    9, TUSB_DESC_INTERFACE, ITF_NUM_CDC1_DATA, 0, 2, TUSB_CLASS_CDC_DATA, 0, 0, 0,
    7, TUSB_DESC_ENDPOINT, 0x02, TUSB_XFER_BULK, U16_TO_U8S_LE(CDC_BULK_EP_SIZE), 0,
    7, TUSB_DESC_ENDPOINT, 0x82, TUSB_XFER_BULK, U16_TO_U8S_LE(CDC_BULK_EP_SIZE), 0,

    // CDC2
    8, TUSB_DESC_INTERFACE_ASSOCIATION, ITF_NUM_CDC2, 2, TUSB_CLASS_CDC, CDC_COMM_SUBCLASS_ABSTRACT_CONTROL_MODEL, CDC_COMM_PROTOCOL_NONE, 0,
    9, TUSB_DESC_INTERFACE, ITF_NUM_CDC2, 0, 0, TUSB_CLASS_CDC, CDC_COMM_SUBCLASS_ABSTRACT_CONTROL_MODEL, CDC_COMM_PROTOCOL_NONE, 0,
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_HEADER, U16_TO_U8S_LE(0x0120),
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_CALL_MANAGEMENT, 0, ITF_NUM_CDC2_DATA,
    4, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_ABSTRACT_CONTROL_MANAGEMENT, 6,
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_UNION, ITF_NUM_CDC2, ITF_NUM_CDC2_DATA,
    9, TUSB_DESC_INTERFACE, ITF_NUM_CDC2_DATA, 0, 2, TUSB_CLASS_CDC_DATA, 0, 0, 0,
    7, TUSB_DESC_ENDPOINT, 0x03, TUSB_XFER_BULK, U16_TO_U8S_LE(CDC_BULK_EP_SIZE), 0,
    7, TUSB_DESC_ENDPOINT, 0x83, TUSB_XFER_BULK, U16_TO_U8S_LE(CDC_BULK_EP_SIZE), 0,

    // CDC3
    8, TUSB_DESC_INTERFACE_ASSOCIATION, ITF_NUM_CDC3, 2, TUSB_CLASS_CDC, CDC_COMM_SUBCLASS_ABSTRACT_CONTROL_MODEL, CDC_COMM_PROTOCOL_NONE, 0,
    9, TUSB_DESC_INTERFACE, ITF_NUM_CDC3, 0, 0, TUSB_CLASS_CDC, CDC_COMM_SUBCLASS_ABSTRACT_CONTROL_MODEL, CDC_COMM_PROTOCOL_NONE, 0,
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_HEADER, U16_TO_U8S_LE(0x0120),
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_CALL_MANAGEMENT, 0, ITF_NUM_CDC3_DATA,
    4, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_ABSTRACT_CONTROL_MANAGEMENT, 6,
    5, TUSB_DESC_CS_INTERFACE, CDC_FUNC_DESC_UNION, ITF_NUM_CDC3, ITF_NUM_CDC3_DATA,
    9, TUSB_DESC_INTERFACE, ITF_NUM_CDC3_DATA, 0, 2, TUSB_CLASS_CDC_DATA, 0, 0, 0,
    7, TUSB_DESC_ENDPOINT, 0x04, TUSB_XFER_BULK, U16_TO_U8S_LE(CDC_BULK_EP_SIZE), 0,
    7, TUSB_DESC_ENDPOINT, 0x84, TUSB_XFER_BULK, U16_TO_U8S_LE(CDC_BULK_EP_SIZE), 0,
};

/**
 * @brief Application Queue
 */
static QueueHandle_t app_queue;
static uint8_t s_uart1_rx_buf[UART_BRIDGE_RX_BUF_SIZE];
static uint8_t s_uart2_rx_buf[UART_BRIDGE_RX_BUF_SIZE];
typedef struct {
    uint8_t buf[CONFIG_TINYUSB_CDC_RX_BUFSIZE + 1];     // Data buffer
    size_t buf_len;                                     // Number of bytes received
    uint8_t itf;                                        // Index of CDC device interface
} app_message_t;

/**
 * @brief Initialize one hardware UART for CDC bridge.
 *
 * @param[in] uart_port Hardware UART index.
 * @param[in] tx_gpio UART TX GPIO number.
 * @param[in] rx_gpio UART RX GPIO number.
 *
 * @return ESP_OK on success, error code otherwise.
 */
static esp_err_t uart_bridge_init(uart_port_t uart_port, int tx_gpio, int rx_gpio)
{
    if ((uart_port < UART_NUM_0) || (uart_port >= UART_NUM_MAX)) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((tx_gpio < 0) || (rx_gpio < 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    const uart_config_t uart_cfg = {
        .baud_rate = UART_BRIDGE_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t ret = uart_param_config(uart_port, &uart_cfg);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = uart_set_pin(uart_port, tx_gpio, rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        return ret;
    }

    if (!uart_is_driver_installed(uart_port)) {
        return uart_driver_install(uart_port, UART_BRIDGE_RX_BUF_SIZE, UART_BRIDGE_TX_BUF_SIZE, 0, NULL, 0);
    }

    return ESP_OK;
}

/**
 * @brief Forward CDC RX payload to mapped hardware UART.
 *
 * @param[in] msg CDC message from queue.
 */
static void cdc_forward_to_uart(const app_message_t *msg)
{
    esp_err_t record_ret = ESP_OK;

    if ((msg == NULL) || (msg->buf_len == 0U)) {
        return;
    }

    if (msg->itf == TINYUSB_CDC_ACM_1) {
        (void)uart_write_bytes(UART_NUM_1, msg->buf, msg->buf_len);
        record_ret = uart_learn_record_data(UART_LEARN_EVENT_TX, 1U, msg->buf, msg->buf_len);
        if ((record_ret != ESP_OK) && (record_ret != ESP_ERR_NO_MEM)) {
            ESP_LOGW(TAG, "Record TX event for port 1 failed: %s", esp_err_to_name(record_ret));
        }
        return;
    }

    if (msg->itf == TINYUSB_CDC_ACM_2) {
        (void)uart_write_bytes(UART_NUM_2, msg->buf, msg->buf_len);
        record_ret = uart_learn_record_data(UART_LEARN_EVENT_TX, 2U, msg->buf, msg->buf_len);
        if ((record_ret != ESP_OK) && (record_ret != ESP_ERR_NO_MEM)) {
            ESP_LOGW(TAG, "Record TX event for port 2 failed: %s", esp_err_to_name(record_ret));
        }
        return;
    }

    // Keep existing behavior for other CDC channels.
    tinyusb_cdcacm_write_queue(msg->itf, msg->buf, msg->buf_len);
    esp_err_t err = tinyusb_cdcacm_write_flush(msg->itf, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CDC ACM write flush error: %s", esp_err_to_name(err));
    }
}

/**
 * @brief Poll one hardware UART and forward data to mapped CDC channel.
 *
 * @param[in] uart_port Hardware UART index.
 * @param[in] cdc_port TinyUSB CDC channel index.
 * @param[in] rx_buf Receive buffer for UART data.
 * @param[in] rx_buf_size Receive buffer size in bytes.
 */
static void uart_forward_to_cdc(uart_port_t uart_port, uint8_t cdc_port, uint8_t *rx_buf, size_t rx_buf_size)
{
    if ((rx_buf == NULL) || (rx_buf_size == 0U)) {
        return;
    }

    int rx_len = uart_read_bytes(uart_port, rx_buf, rx_buf_size, 0);
    if (rx_len <= 0) {
        return;
    }

    tinyusb_cdcacm_write_queue(cdc_port, rx_buf, (size_t)rx_len);
    esp_err_t err = tinyusb_cdcacm_write_flush(cdc_port, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CDC%d flush from UART%d failed: %s", cdc_port, uart_port, esp_err_to_name(err));
    }

    if (uart_port == UART_NUM_1) {
        err = uart_learn_record_data(UART_LEARN_EVENT_RX, 1U, rx_buf, (size_t)rx_len);
        if ((err != ESP_OK) && (err != ESP_ERR_NO_MEM)) {
            ESP_LOGW(TAG, "Record RX event for port 1 failed: %s", esp_err_to_name(err));
        }
        return;
    }

    if (uart_port == UART_NUM_2) {
        err = uart_learn_record_data(UART_LEARN_EVENT_RX, 2U, rx_buf, (size_t)rx_len);
        if ((err != ESP_OK) && (err != ESP_ERR_NO_MEM)) {
            ESP_LOGW(TAG, "Record RX event for port 2 failed: %s", esp_err_to_name(err));
        }
    }
}

/**
 * @brief CDC device RX callback
 *
 * CDC device signals, that new data were received
 *
 * @param[in] itf   CDC device index
 * @param[in] event CDC event type
 */
void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    (void)event;
    /* initialization */
    size_t rx_size = 0;

    /* read */
    esp_err_t ret = tinyusb_cdcacm_read(itf, rx_buf, CONFIG_TINYUSB_CDC_RX_BUFSIZE, &rx_size);
    if (ret == ESP_OK) {

        app_message_t tx_msg = {
            .buf_len = rx_size,
            .itf = itf,
        };

        memcpy(tx_msg.buf, rx_buf, rx_size);
        xQueueSend(app_queue, &tx_msg, 0);
    } else {
        ESP_LOGE(TAG, "Read Error");
    }
}

/**
 * @brief CDC device line change callback
 *
 * CDC device signals, that the DTR, RTS states changed
 *
 * @param[in] itf   CDC device index
 * @param[in] event CDC event type
 */
void tinyusb_cdc_line_state_changed_callback(int itf, cdcacm_event_t *event)
{
    int dtr = event->line_state_changed_data.dtr;
    int rts = event->line_state_changed_data.rts;
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Line state changed on channel %d: DTR:%d, RTS:%d", itf, dtr, rts);

    if ((itf == TINYUSB_CDC_ACM_1) || (itf == TINYUSB_CDC_ACM_2)) {
        ret = uart_learn_record_line_state((uint8_t)itf, (dtr != 0), (rts != 0));
        if ((ret != ESP_OK) && (ret != ESP_ERR_NO_MEM)) {
            ESP_LOGW(TAG, "Record line state for channel %d failed: %s", itf, esp_err_to_name(ret));
        }
    }
}

esp_err_t usb_cdc_bridge_init(void)
{
    tinyusb_config_t tusb_cfg = {0};
    tinyusb_config_cdcacm_t acm_cfg = {0};

    if (app_queue != NULL) {
        return ESP_OK;
    }

    app_queue = xQueueCreate(5, sizeof(app_message_t));
    if (app_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "USB initialization");
    tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.full_speed_config = g_cdc4_no_notify_fs_desc;
    ESP_RETURN_ON_ERROR(tinyusb_driver_install(&tusb_cfg), TAG, "Install TinyUSB driver failed");

    acm_cfg.cdc_port = TINYUSB_CDC_ACM_0;
    acm_cfg.callback_rx = &tinyusb_cdc_rx_callback;
    acm_cfg.callback_rx_wanted_char = NULL;
    acm_cfg.callback_line_state_changed = NULL;
    acm_cfg.callback_line_coding_changed = NULL;

    ESP_RETURN_ON_ERROR(tinyusb_cdcacm_init(&acm_cfg), TAG, "Init CDC0 failed");
    ESP_RETURN_ON_ERROR(
        tinyusb_cdcacm_register_callback(
            TINYUSB_CDC_ACM_0,
            CDC_EVENT_LINE_STATE_CHANGED,
            &tinyusb_cdc_line_state_changed_callback),
        TAG,
        "Register CDC0 line state callback failed");

#if (CONFIG_TINYUSB_CDC_COUNT > 1)
    acm_cfg.cdc_port = TINYUSB_CDC_ACM_1;
    ESP_RETURN_ON_ERROR(tinyusb_cdcacm_init(&acm_cfg), TAG, "Init CDC1 failed");
    ESP_RETURN_ON_ERROR(
        tinyusb_cdcacm_register_callback(
            TINYUSB_CDC_ACM_1,
            CDC_EVENT_LINE_STATE_CHANGED,
            &tinyusb_cdc_line_state_changed_callback),
        TAG,
        "Register CDC1 line state callback failed");
#endif

#if (CONFIG_TINYUSB_CDC_COUNT > 2)
    acm_cfg.cdc_port = TINYUSB_CDC_ACM_2;
    ESP_RETURN_ON_ERROR(tinyusb_cdcacm_init(&acm_cfg), TAG, "Init CDC2 failed");
    ESP_RETURN_ON_ERROR(
        tinyusb_cdcacm_register_callback(
            TINYUSB_CDC_ACM_2,
            CDC_EVENT_LINE_STATE_CHANGED,
            &tinyusb_cdc_line_state_changed_callback),
        TAG,
        "Register CDC2 line state callback failed");
#endif

#if (CONFIG_TINYUSB_CDC_COUNT > 3)
    acm_cfg.cdc_port = TINYUSB_CDC_ACM_3;
    ESP_RETURN_ON_ERROR(tinyusb_cdcacm_init(&acm_cfg), TAG, "Init CDC3 failed");
    ESP_RETURN_ON_ERROR(
        tinyusb_cdcacm_register_callback(
            TINYUSB_CDC_ACM_3,
            CDC_EVENT_LINE_STATE_CHANGED,
            &tinyusb_cdc_line_state_changed_callback),
        TAG,
        "Register CDC3 line state callback failed");
#endif

    ESP_RETURN_ON_ERROR(uart_bridge_init(UART_NUM_1, UART1_TX_GPIO, UART1_RX_GPIO), TAG, "Init UART1 bridge failed");
    ESP_RETURN_ON_ERROR(uart_bridge_init(UART_NUM_2, UART2_TX_GPIO, UART2_RX_GPIO), TAG, "Init UART2 bridge failed");

    ESP_LOGI(TAG, "USB initialization DONE");

    return ESP_OK;
}

void usb_cdc_bridge_process(void)
{
    app_message_t msg;

    if (app_queue == NULL) {
        return;
    }

    if (xQueueReceive(app_queue, &msg, pdMS_TO_TICKS(10)) == pdTRUE) {
        cdc_forward_to_uart(&msg);
    }

    uart_forward_to_cdc(UART_NUM_1, TINYUSB_CDC_ACM_1, s_uart1_rx_buf, sizeof(s_uart1_rx_buf));
    uart_forward_to_cdc(UART_NUM_2, TINYUSB_CDC_ACM_2, s_uart2_rx_buf, sizeof(s_uart2_rx_buf));
}
