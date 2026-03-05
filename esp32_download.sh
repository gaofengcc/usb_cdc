#!/bin/bash
# ESP32 Project Download Script
# Flashes bootloader, app and partition table to device via esptool

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}"
IDF_PATH="/e/code/esp32_Project/v5.5.2/esp-idf"
TOOLS_PATH="/e/code/esp32_Project/tools"

# Serial port: from .vscode/settings.json "idf.portWin" if present, else COM5; CLI arg overrides
SERIAL_PORT="COM5"
VSCODE_SETTINGS="${PROJECT_DIR}/.vscode/settings.json"
if [ -f "${VSCODE_SETTINGS}" ]; then
    _port=$(grep '"idf.portWin"' "${VSCODE_SETTINGS}" 2>/dev/null | sed -n 's/.*"idf.portWin"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' | tr -d '\r' | head -1)
    if [ -n "${_port}" ]; then
        SERIAL_PORT="${_port}"
    fi
fi
if [ -n "${1}" ]; then
    SERIAL_PORT="${1}"
fi
BAUD_RATE="460800"
CHIP="esp32s3"
FLASH_MODE="dio"
FLASH_FREQ="80m"
FLASH_SIZE="2MB"

BUILD_DIR="${PROJECT_DIR}/build"
BOOTLOADER_BIN="${BUILD_DIR}/bootloader/bootloader.bin"
APP_BIN="${BUILD_DIR}/USB_CDC.bin"
PARTITION_BIN="${BUILD_DIR}/partition_table/partition-table.bin"
ESPTOOL_PY="${IDF_PATH}/components/esptool_py/esptool/esptool.py"
PYTHON="${TOOLS_PATH}/python_env/idf5.5_py3.11_env/Scripts/python.exe"

echo -e "${GREEN}ESP32 download (flash) script${NC}"
echo -e "${YELLOW}Port: ${SERIAL_PORT}, baud: ${BAUD_RATE}${NC}"

if [ ! -f "${PYTHON}" ]; then
    echo -e "${RED}Python not found: ${PYTHON}${NC}"
    exit 1
fi
if [ ! -f "${ESPTOOL_PY}" ]; then
    echo -e "${RED}esptool.py not found: ${ESPTOOL_PY}${NC}"
    exit 1
fi
if [ ! -f "${BOOTLOADER_BIN}" ]; then
    echo -e "${RED}Bootloader not found: ${BOOTLOADER_BIN}. Build first (e.g. ./build_esp32.sh)${NC}"
    exit 1
fi
if [ ! -f "${APP_BIN}" ]; then
    echo -e "${RED}App binary not found: ${APP_BIN}. Build first.${NC}"
    exit 1
fi
if [ ! -f "${PARTITION_BIN}" ]; then
    echo -e "${RED}Partition table not found: ${PARTITION_BIN}. Build first.${NC}"
    exit 1
fi

echo -e "${YELLOW}Flashing...${NC}"
"${PYTHON}" "${ESPTOOL_PY}" -p "${SERIAL_PORT}" -b "${BAUD_RATE}" \
    --before default_reset --after hard_reset --chip "${CHIP}" \
    write_flash --flash_mode "${FLASH_MODE}" --flash_freq "${FLASH_FREQ}" --flash_size "${FLASH_SIZE}" \
    0x0 "${BOOTLOADER_BIN}" \
    0x10000 "${APP_BIN}" \
    0x8000 "${PARTITION_BIN}"

echo -e "${GREEN}Download completed.${NC}"
