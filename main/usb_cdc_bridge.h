#ifndef USB_CDC_BRIDGE_H
#define USB_CDC_BRIDGE_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize TinyUSB CDC and UART bridge resources.
 *
 * @return ESP_OK on success, error code otherwise.
 */
esp_err_t usb_cdc_bridge_init(void);

/**
 * @brief Process one bridge cycle for TX and RX forwarding.
 */
void usb_cdc_bridge_process(void);

#ifdef __cplusplus
}
#endif

#endif
