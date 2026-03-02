#!/bin/bash
# ESP32 Project Build Script
# This script configures environment and builds the ESP32 project

set -e  # Exit on error

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Starting ESP32 project build...${NC}"

# Project paths (script's directory as project root)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="${SCRIPT_DIR}"
IDF_PATH="/e/code/esp32_Project/v5.5.2/esp-idf"
TOOLS_PATH="/e/code/esp32_Project/tools"

# Environment setup
echo -e "${YELLOW}Setting up environment...${NC}"
export IDF_PATH="${IDF_PATH}"
export PYTHON="${TOOLS_PATH}/python_env/idf5.5_py3.11_env/Scripts/python.exe"
export PATH="${TOOLS_PATH}/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin:${TOOLS_PATH}/tools/ninja/1.12.1:${TOOLS_PATH}/tools/cmake/3.30.2/bin:$PATH"

# Change to project directory
cd "${PROJECT_DIR}" || {
    echo -e "${RED}Failed to change to project directory${NC}"
    exit 1
}

# Clean build directory (optional, comment out if you want incremental builds)
# On Windows, files may be locked by other processes; ignore clean failure and continue
echo -e "${YELLOW}Cleaning build directory...${NC}"
if ! rm -rf build 2>/dev/null; then
    echo -e "${YELLOW}Warning: Could not fully remove build directory (files may be in use). Continuing with incremental build...${NC}"
fi

# CMake configuration
echo -e "${YELLOW}Configuring project with CMake...${NC}"
cmake -G Ninja \
    -DPYTHON="${PYTHON}" \
    -DPYTHON_DEPS_CHECKED=1 \
    -DESP_PLATFORM=1 \
    -B build \
    -S . \
    -DSDKCONFIG="sdkconfig" || {
    echo -e "${RED}CMake configuration failed!${NC}"
    exit 1
}

# Build project
echo -e "${YELLOW}Building project...${NC}"
cmake --build build || {
    echo -e "${RED}Build failed!${NC}"
    exit 1
}

echo -e "${GREEN}Build completed successfully!${NC}"
echo -e "${GREEN}Binary location: ${PROJECT_DIR}/build/hello_world.bin${NC}"
