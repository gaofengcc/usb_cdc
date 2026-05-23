#include "uart_learn.h"

#include <inttypes.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define UART_LEARN_MAX_TRACKED_PORTS 4U
#define UART_LEARN_LOG_BUFFER_LEN    ((UART_LEARN_MAX_EVENT_DATA_LEN * 3U) + 1U)

typedef struct {
    uart_learn_event_type_t event_type;
    uint8_t port_id;
    size_t data_len;
    uint8_t data[UART_LEARN_MAX_EVENT_DATA_LEN];
    bool dtr;
    bool rts;
    uint64_t timestamp_us;
} uart_learn_event_t;

static const char *TAG = "uart_learn";

static QueueHandle_t s_uart_learn_queue;
static TaskHandle_t s_uart_learn_task_handle;
static uart_learn_config_t s_uart_learn_config;
static uart_learn_status_t s_uart_learn_status = UART_LEARN_STATUS_IDLE;
static uart_learn_port_stats_t s_port_stats[UART_LEARN_MAX_TRACKED_PORTS];
static bool s_uart_learn_task_running;

/**
 * @brief Convert one binary buffer into readable hex text.
 *
 * @param[in] data Source buffer.
 * @param[in] data_len Source length in bytes.
 * @param[out] hex_buffer Destination text buffer.
 * @param[in] hex_buffer_len Destination text buffer size.
 */
static void uart_learn_format_hex(
    const uint8_t *data,
    size_t data_len,
    char *hex_buffer,
    size_t hex_buffer_len)
{
    static const char hex_table[] = "0123456789ABCDEF";
    size_t write_offset = 0U;
    size_t index = 0U;

    if ((data == NULL) || (hex_buffer == NULL) || (hex_buffer_len == 0U)) {
        return;
    }

    hex_buffer[0] = '\0';

    for (index = 0U; index < data_len; index++) {
        if ((write_offset + 3U) >= hex_buffer_len) {
            break;
        }

        hex_buffer[write_offset++] = hex_table[(data[index] >> 4U) & 0x0FU];
        hex_buffer[write_offset++] = hex_table[data[index] & 0x0FU];
        hex_buffer[write_offset++] = (index + 1U < data_len) ? ' ' : '\0';
    }

    if (write_offset >= hex_buffer_len) {
        hex_buffer[hex_buffer_len - 1U] = '\0';
    } else if ((write_offset > 0U) && (hex_buffer[write_offset - 1U] == ' ')) {
        hex_buffer[write_offset - 1U] = '\0';
    } else {
        hex_buffer[write_offset] = '\0';
    }
}

/**
 * @brief Update runtime statistics for one event.
 *
 * @param[in] event UART learn event.
 */
static void uart_learn_update_stats(const uart_learn_event_t *event)
{
    uart_learn_port_stats_t *port_stats = NULL;

    if ((event == NULL) || (event->port_id >= UART_LEARN_MAX_TRACKED_PORTS)) {
        return;
    }

    port_stats = &s_port_stats[event->port_id];
    port_stats->port_id = event->port_id;
    port_stats->last_event_timestamp_us = event->timestamp_us;

    if (event->event_type == UART_LEARN_EVENT_TX) {
        port_stats->tx_event_count++;
        return;
    }

    if (event->event_type == UART_LEARN_EVENT_RX) {
        port_stats->rx_event_count++;
        return;
    }

    if (event->event_type == UART_LEARN_EVENT_LINE_STATE) {
        port_stats->line_state_event_count++;
    }
}

/**
 * @brief Print one learn event when log output is enabled.
 *
 * @param[in] event UART learn event.
 */
static void uart_learn_log_event(const uart_learn_event_t *event)
{
    char hex_buffer[UART_LEARN_LOG_BUFFER_LEN];

    if ((event == NULL) || (!s_uart_learn_config.enable_log_output)) {
        return;
    }

    if ((event->event_type == UART_LEARN_EVENT_TX) || (event->event_type == UART_LEARN_EVENT_RX)) {
        uart_learn_format_hex(event->data, event->data_len, hex_buffer, sizeof(hex_buffer));
        ESP_LOGI(
            TAG,
            "port=%u type=%s len=%u ts=%" PRIu64 "us data=%s",
            event->port_id,
            (event->event_type == UART_LEARN_EVENT_TX) ? "tx" : "rx",
            (unsigned int)event->data_len,
            event->timestamp_us,
            hex_buffer);
        return;
    }

    ESP_LOGI(
        TAG,
        "port=%u type=line_state ts=%" PRIu64 "us dtr=%d rts=%d",
        event->port_id,
        event->timestamp_us,
        event->dtr ? 1 : 0,
        event->rts ? 1 : 0);
}

/**
 * @brief UART learn worker task.
 *
 * @param[in] parameter Unused task parameter.
 */
static void uart_learn_task(void *parameter)
{
    uart_learn_event_t event;

    (void)parameter;

    while (s_uart_learn_task_running) {
        if ((s_uart_learn_queue != NULL) &&
            (xQueueReceive(s_uart_learn_queue, &event, pdMS_TO_TICKS(20)) == pdTRUE)) {
            uart_learn_update_stats(&event);
            uart_learn_log_event(&event);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    s_uart_learn_task_handle = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief Push one learn event into the engine queue.
 *
 * @param[in] event Prepared UART learn event.
 *
 * @return ESP_OK on success, error code otherwise.
 */
static esp_err_t uart_learn_push_event(const uart_learn_event_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_uart_learn_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xQueueSend(s_uart_learn_queue, event, 0) != pdTRUE) {
        if (event->port_id < UART_LEARN_MAX_TRACKED_PORTS) {
            s_port_stats[event->port_id].dropped_event_count++;
        }
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t uart_learn_init(const uart_learn_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((config->queue_depth == 0U) ||
        (config->task_stack_size == 0U) ||
        (config->task_priority == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_uart_learn_queue != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(s_port_stats, 0, sizeof(s_port_stats));
    s_uart_learn_config = *config;
    s_uart_learn_queue = xQueueCreate(
        (UBaseType_t)s_uart_learn_config.queue_depth,
        sizeof(uart_learn_event_t));

    if (s_uart_learn_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_uart_learn_status = UART_LEARN_STATUS_READY;
    return ESP_OK;
}

esp_err_t uart_learn_start(void)
{
    BaseType_t create_result;

    if (s_uart_learn_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_uart_learn_task_handle != NULL) {
        return ESP_OK;
    }

    s_uart_learn_task_running = true;
    create_result = xTaskCreate(
        uart_learn_task,
        "uart_learn_task",
        (uint32_t)s_uart_learn_config.task_stack_size,
        NULL,
        (UBaseType_t)s_uart_learn_config.task_priority,
        &s_uart_learn_task_handle);

    if (create_result != pdPASS) {
        s_uart_learn_task_running = false;
        return ESP_ERR_NO_MEM;
    }

    s_uart_learn_status = UART_LEARN_STATUS_RUNNING;
    return ESP_OK;
}

esp_err_t uart_learn_stop(void)
{
    if (s_uart_learn_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    s_uart_learn_task_running = false;

    if (s_uart_learn_task_handle != NULL) {
        vTaskDelete(s_uart_learn_task_handle);
        s_uart_learn_task_handle = NULL;
    }

    vQueueDelete(s_uart_learn_queue);
    s_uart_learn_queue = NULL;
    s_uart_learn_status = UART_LEARN_STATUS_STOPPED;
    return ESP_OK;
}

esp_err_t uart_learn_record_data(
    uart_learn_event_type_t event_type,
    uint8_t port_id,
    const uint8_t *data,
    size_t data_len)
{
    uart_learn_event_t event;
    size_t copy_len = 0U;

    if ((event_type != UART_LEARN_EVENT_TX) && (event_type != UART_LEARN_EVENT_RX)) {
        return ESP_ERR_INVALID_ARG;
    }

    if ((data == NULL) || (data_len == 0U)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (port_id >= UART_LEARN_MAX_TRACKED_PORTS) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&event, 0, sizeof(event));
    copy_len = (data_len > UART_LEARN_MAX_EVENT_DATA_LEN) ? UART_LEARN_MAX_EVENT_DATA_LEN : data_len;
    event.event_type = event_type;
    event.port_id = port_id;
    event.data_len = copy_len;
    event.timestamp_us = (uint64_t)esp_timer_get_time();
    memcpy(event.data, data, copy_len);

    return uart_learn_push_event(&event);
}

esp_err_t uart_learn_record_line_state(uint8_t port_id, bool dtr, bool rts)
{
    uart_learn_event_t event;

    if (port_id >= UART_LEARN_MAX_TRACKED_PORTS) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&event, 0, sizeof(event));
    event.event_type = UART_LEARN_EVENT_LINE_STATE;
    event.port_id = port_id;
    event.dtr = dtr;
    event.rts = rts;
    event.timestamp_us = (uint64_t)esp_timer_get_time();

    return uart_learn_push_event(&event);
}

uart_learn_status_t uart_learn_get_status(void)
{
    return s_uart_learn_status;
}

esp_err_t uart_learn_get_port_stats(uint8_t port_id, uart_learn_port_stats_t *stats)
{
    if ((stats == NULL) || (port_id >= UART_LEARN_MAX_TRACKED_PORTS)) {
        return ESP_ERR_INVALID_ARG;
    }

    *stats = s_port_stats[port_id];
    return ESP_OK;
}

esp_err_t uart_learn_set_port_mapping(uint8_t mapping_index, const uart_learn_port_mapping_t *mapping)
{
    if ((mapping == NULL) || (mapping_index >= UART_LEARN_MAX_PORT_MAPPINGS)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_uart_learn_status == UART_LEARN_STATUS_RUNNING) {
        return ESP_ERR_INVALID_STATE;
    }

    s_uart_learn_config.port_mappings[mapping_index] = *mapping;
    return ESP_OK;
}

esp_err_t uart_learn_get_port_mapping(uint8_t mapping_index, uart_learn_port_mapping_t *mapping)
{
    if ((mapping == NULL) || (mapping_index >= UART_LEARN_MAX_PORT_MAPPINGS)) {
        return ESP_ERR_INVALID_ARG;
    }

    *mapping = s_uart_learn_config.port_mappings[mapping_index];
    return ESP_OK;
}

esp_err_t uart_learn_get_mapped_uart_for_cdc(uint8_t cdc_port, uint8_t *uart_port, int *tx_gpio, int *rx_gpio)
{
    uint8_t i;

    if (uart_port == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (i = 0U; i < UART_LEARN_MAX_PORT_MAPPINGS; i++) {
        if ((s_uart_learn_config.port_mappings[i].enabled) &&
            (s_uart_learn_config.port_mappings[i].cdc_port == cdc_port)) {
            *uart_port = s_uart_learn_config.port_mappings[i].uart_port;
            if (tx_gpio != NULL) {
                *tx_gpio = s_uart_learn_config.port_mappings[i].tx_gpio;
            }
            if (rx_gpio != NULL) {
                *rx_gpio = s_uart_learn_config.port_mappings[i].rx_gpio;
            }
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t uart_learn_get_mapped_cdc_for_uart(uint8_t uart_port, uint8_t *cdc_port)
{
    uint8_t i;

    if (cdc_port == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (i = 0U; i < UART_LEARN_MAX_PORT_MAPPINGS; i++) {
        if ((s_uart_learn_config.port_mappings[i].enabled) &&
            (s_uart_learn_config.port_mappings[i].uart_port == uart_port)) {
            *cdc_port = s_uart_learn_config.port_mappings[i].cdc_port;
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

uint8_t uart_learn_get_active_mapping_count(void)
{
    uint8_t i;
    uint8_t count = 0U;

    for (i = 0U; i < UART_LEARN_MAX_PORT_MAPPINGS; i++) {
        if (s_uart_learn_config.port_mappings[i].enabled) {
            count++;
        }
    }

    return count;
}
