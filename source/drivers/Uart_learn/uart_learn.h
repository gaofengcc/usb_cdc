#ifndef UART_LEARN_H
#define UART_LEARN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_LEARN_MAX_EVENT_DATA_LEN    64U
#define UART_LEARN_DEFAULT_QUEUE_DEPTH   16U
#define UART_LEARN_DEFAULT_TASK_STACK    4096U
#define UART_LEARN_DEFAULT_TASK_PRIORITY 5U
#define UART_LEARN_MAX_PORT_MAPPINGS     4U

typedef enum {
    UART_LEARN_EVENT_TX = 0,
    UART_LEARN_EVENT_RX,
    UART_LEARN_EVENT_LINE_STATE,
} uart_learn_event_type_t;

/**
 * @brief Port mapping configuration structure.
 *
 * Defines the mapping between host CDC port and device UART port.
 */
typedef struct {
    uint8_t cdc_port;      /* Host CDC port index (e.g., TINYUSB_CDC_ACM_1) */
    uint8_t uart_port;     /* Device UART port number (e.g., UART_NUM_1) */
    int tx_gpio;           /* UART TX GPIO number */
    int rx_gpio;           /* UART RX GPIO number */
    bool enabled;          /* Whether this mapping is active */
} uart_learn_port_mapping_t;

typedef enum {
    UART_LEARN_STATUS_IDLE = 0,
    UART_LEARN_STATUS_READY,
    UART_LEARN_STATUS_RUNNING,
    UART_LEARN_STATUS_STOPPED,
} uart_learn_status_t;

typedef struct {
    uint8_t port_id;
    uint32_t tx_event_count;
    uint32_t rx_event_count;
    uint32_t line_state_event_count;
    uint32_t dropped_event_count;
    uint64_t last_event_timestamp_us;
} uart_learn_port_stats_t;

typedef struct {
    uint32_t queue_depth;
    uint32_t task_stack_size;
    uint32_t task_priority;
    bool enable_log_output;
    uart_learn_port_mapping_t port_mappings[UART_LEARN_MAX_PORT_MAPPINGS];
} uart_learn_config_t;

#define UART_LEARN_DEFAULT_CONFIG()                                      \
    {                                                                    \
        .queue_depth = UART_LEARN_DEFAULT_QUEUE_DEPTH,                   \
        .task_stack_size = UART_LEARN_DEFAULT_TASK_STACK,                  \
        .task_priority = UART_LEARN_DEFAULT_TASK_PRIORITY,                 \
        .enable_log_output = true,                                         \
        .port_mappings = {                                                 \
            {1, 1, 17, 18, true},   /* CDC1 -> UART1 (GPIO17/18) */         \
            {2, 2, 15, 16, true},   /* CDC2 -> UART2 (GPIO15/16) */         \
            {0, 0, -1, -1, false},  /* Slot 3: disabled */                   \
            {0, 0, -1, -1, false}   /* Slot 4: disabled */                   \
        }                                                                  \
    }

/**
 * @brief Initialize the UART learn engine.
 *
 * @param[in] config Engine configuration.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t uart_learn_init(const uart_learn_config_t *config);

/**
 * @brief Start the UART learn engine task.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t uart_learn_start(void);

/**
 * @brief Stop the UART learn engine task and release resources.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t uart_learn_stop(void);

/**
 * @brief Record one TX or RX data event.
 *
 * @param[in] event_type Event type, must be TX or RX.
 * @param[in] port_id Logical port identifier.
 * @param[in] data Data buffer.
 * @param[in] data_len Data length in bytes.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t uart_learn_record_data(
    uart_learn_event_type_t event_type,
    uint8_t port_id,
    const uint8_t *data,
    size_t data_len);

/**
 * @brief Record one DTR or RTS line state event.
 *
 * @param[in] port_id Logical port identifier.
 * @param[in] dtr DTR state.
 * @param[in] rts RTS state.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t uart_learn_record_line_state(uint8_t port_id, bool dtr, bool rts);

/**
 * @brief Get current learn engine status.
 *
 * @return Current engine status.
 */
uart_learn_status_t uart_learn_get_status(void);

/**
 * @brief Get collected statistics for one logical port.
 *
 * @param[in] port_id Logical port identifier.
 * @param[out] stats Output statistics pointer.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t uart_learn_get_port_stats(uint8_t port_id, uart_learn_port_stats_t *stats);

/**
 * @brief Set port mapping configuration.
 *
 * Configures the mapping between a host CDC port and a device UART port.
 * Must be called before uart_learn_start() if changing default mappings.
 *
 * @param[in] mapping_index Mapping slot index (0 to UART_LEARN_MAX_PORT_MAPPINGS-1).
 * @param[in] mapping Pointer to port mapping configuration.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t uart_learn_set_port_mapping(uint8_t mapping_index, const uart_learn_port_mapping_t *mapping);

/**
 * @brief Get port mapping configuration.
 *
 * @param[in] mapping_index Mapping slot index.
 * @param[out] mapping Output port mapping pointer.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t uart_learn_get_port_mapping(uint8_t mapping_index, uart_learn_port_mapping_t *mapping);

/**
 * @brief Get UART port mapped to a specific CDC port.
 *
 * @param[in] cdc_port CDC port index.
 * @param[out] uart_port Output UART port number.
 * @param[out] tx_gpio Output TX GPIO (can be NULL).
 * @param[out] rx_gpio Output RX GPIO (can be NULL).
 *
 * @return ESP_OK if mapping found, ESP_ERR_NOT_FOUND otherwise.
 */
esp_err_t uart_learn_get_mapped_uart_for_cdc(uint8_t cdc_port, uint8_t *uart_port, int *tx_gpio, int *rx_gpio);

/**
 * @brief Get CDC port mapped to a specific UART port.
 *
 * @param[in] uart_port UART port number.
 * @param[out] cdc_port Output CDC port index.
 *
 * @return ESP_OK if mapping found, ESP_ERR_NOT_FOUND otherwise.
 */
esp_err_t uart_learn_get_mapped_cdc_for_uart(uint8_t uart_port, uint8_t *cdc_port);

/**
 * @brief Get number of active port mappings.
 *
 * @return Number of enabled port mappings.
 */
uint8_t uart_learn_get_active_mapping_count(void);

#ifdef __cplusplus
}
#endif

#endif
