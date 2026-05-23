idf_component_register(
    SRCS
        "main.c"
        "tusb_serial_device_main.c"
        "../source/drivers/Uart_learn/uart_learn.c"
    INCLUDE_DIRS
        "."
        "../source/drivers/Uart_learn"
        "../source/drivers/LCD35/include"
    REQUIRES
        driver
        esp_timer
        nvs_flash
        freertos
        log
        LCD35
)

