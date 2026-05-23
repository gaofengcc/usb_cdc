# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

add_compile_options(-w) # Turn off warnings until esp_tinyusb is updated IEC-86
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# LCD 驱动 (submodule: LCD_Drivers, 分支 esp32_ILI9488)
# 克隆后执行: git submodule update --init source/drivers/LCD35
list(APPEND EXTRA_COMPONENT_DIRS "source/drivers/LCD35")

project(USB_CDC)
