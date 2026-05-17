# USB_CDC

## 项目简介

这是一个基于 `ESP32-S3` 的通用下载器项目仓库。项目的长期目标是做一个低成本、可扩展的学习型烧录平台。

核心目标分为两部分：

1. **学习阶段**：通过串口代理记录 PC 下载工具和目标芯片之间的下载交互，产出结构化 `JSON` 模板。
2. **回放阶段**：让 `ESP32` 脱离 PC，按模板完成复位、发帧、等 ACK、重试和下载。

**当前 P0 主线**：ESP32 串口透传学习方式（串口代理学习 + ESP32 UART/GPIO 回放）。

## 项目预期

当前项目的总体预期来自 `agents_doc/` 下的方案文档：

- **P0（当前）**：串口代理学习 + ESP32 回放。聚焦 `UART Bootloader` 类协议，优先覆盖 `ESP32`、`STM32 UART Bootloader` 等场景。
- **P1**：模板管理、回放状态机、错误恢复、复杂 ACK/重试场景。
- **P2**：I2S/IIS 被动监听、波形级分析、高速协议扩展。

串口代理学习相比 I2S 波形监听的优点：

- 不需要先做波特率识别和帧同步算法
- 直接拿到字节级协议数据，模板更稳定
- 更贴近当前仓库已具备的 `USB CDC ↔ UART` 桥接基础
- 开发和验证闭环更短

项目更详细的技术规划见：

- `agents_doc/01-可行性分析.md`
- `agents_doc/02-方案简述.md`
- `agents_doc/03-UART方案详解.md`
- `agents_doc/04-架构设计文档.md`
- `agents_doc/06-COM代理方案.md`

## 当前仓库状态

仓库目前处于**"P0 主线开发"**阶段，重点是打通串口代理学习到 ESP32 回放的完整链路。

当前已知状态：

- **固件侧**：当前主入口是 `main/tusb_serial_device_main.c`，已实现 USB CDC 多通道桥接到硬件 UART。
- **新增目录**：`source/drivers/Uart_learn/` 和 `source/drivers/IIS_learn/` 已创建，用于后续驱动开发。
- **PC 侧工具**：`tools/com_proxy/` 已实现串口代理、会话记录和协议模板导出。
- **规划模块**：`Learner`、`Player`、`Storage` 等模块在 `agents_doc/04-架构设计文档.md` 中有完整定义，正在逐步实现。

也就是说，当前仓库是**"固件桥接基础 + PC 侧学习工具 + 驱动目录初始化 + 完整设计文档"**的组合状态。

## 目录说明

```text
USB_CDC/
├── agents_doc/              # 项目设计、方案分析、测试和架构文档
├── main/                    # 当前 ESP-IDF 固件入口（CDC 桥接原型）
├── source/
│   └── drivers/
│       ├── Uart_learn/      # UART 学习与回放驱动（待实现）
│       └── IIS_learn/       # I2S 被动监听驱动（P2 阶段）
├── tools/com_proxy/         # PC 侧 COM 代理工具，负责记录并导出 JSON 模板
├── drivers/                 # 预留目录
├── build_esp32.sh           # 构建脚本
├── esp32_download.sh        # 下载脚本
└── pytest_usb_device_serial.py
```

## 已实现内容

### 1. 固件原型（main/）

当前 `main/tusb_serial_device_main.c` 已实现：

- USB CDC 枚举和数据收发
- 4 通道 CDC 设备描述符
- CDC0/CDC1 映射到硬件 UART1/UART2 的桥接逻辑
- 基础 UART 收发和数据转发

当前可用作串口代理学习的硬件入口：下载工具 → USB CDC → ESP32 → 目标芯片 UART。

### 2. PC 侧 COM Proxy 工具

`tools/com_proxy/` 是当前最接近项目目标的 PC 侧工具：

- 代理虚拟串口和真实串口之间的通信
- 记录下载会话中的 TX、RX、RTS、DTR
- 按当前规则匹配 ACK 和帧间隔
- 导出可供 ESP32 回放端使用的 `JSON` 模板

详细使用方式见 `tools/com_proxy/README.md`。

### 3. 新增驱动目录

- `source/drivers/Uart_learn/`：预留用于 UART 学习与回放驱动开发
- `source/drivers/IIS_learn/`：预留用于 I2S 被动监听驱动开发（P2 阶段）

## 推荐使用路径

如果你是第一次接触本项目，建议按下面顺序理解和使用：

1. 先读 `agents_doc/01-可行性分析.md` 和 `agents_doc/02-方案简述.md`，了解项目目标和分阶段路线。
2. 再读 `agents_doc/03-UART方案详解.md`，理解为什么当前优先用串口代理而不是 I2S 监听。
3. 读 `agents_doc/04-架构设计文档.md`，了解规划中的模块划分和模板格式。
4. 如果要先验证现有成果，使用 `tools/com_proxy/` 进行 PC 侧协议记录。
5. 如果要调试板端固件，查看 `main/tusb_serial_device_main.c` 的 USB CDC 桥接实现。

## 固件构建

项目当前仍是标准 ESP-IDF 工程，常用构建方式如下：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

或者根据仓库内脚本执行：

```bash
./build_esp32.sh
./esp32_download.sh
```

具体可用性取决于本机 ESP-IDF 环境和串口配置。

## COM Proxy 快速开始

先安装 Python 依赖：

```bash
pip install pyserial
```

然后运行：

```bash
python tools/com_proxy/main.py --help
```

完整使用说明见 `tools/com_proxy/README.md`。

## 当前限制

在使用项目之前，请先明确下面几点：

- **当前仓库还没有实现完整的 ESP32 侧学习引擎固件和回放引擎固件**，只有基础 CDC 桥接。
- `source/drivers/Uart_learn/` 和 `source/drivers/IIS_learn/` 是刚创建的目录，驱动实现待补充。
- 当前 `JSON` 模板是第一版格式，后续仍可能根据回放端需求调整。
- 当前 COM 代理工具更适合 UART 类工具链，不是全协议通用抓包器。
- 设计文档覆盖范围大于当前代码实现范围，阅读时请区分"已实现"和"规划中"。

## 后续开发重点

接下来最重要的方向通常包括：

1. **收敛并稳定 `JSON` 模板格式**。
2. **实现 `source/drivers/Uart_learn/` 中的 UART 学习与回放驱动**：
   - 模板加载和解析
   - 按帧回放 + GPIO 复位控制
   - ACK 接收 + 超时重试逻辑
3. **增加更严格的测试**，区分 Host 侧算法测试和板端功能测试。
4. **评估去除对 Windows 虚拟串口驱动的强依赖**，提升跨平台可用性。
5. **I2S 被动监听**作为 P2 阶段能力，待 P0 主线稳定后再评估引入。

## 给开发者和 AI Agent

如果你是来接手本项目开发和调试的开发者或 AI agent，请优先阅读根目录的 `agent_readme.md`。
