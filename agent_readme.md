# Agent Readme

## 1. 文档定位

这份文档面向接手本项目的 AI 编程 agent 和开发者。目标是让新接手的人快速理解：

- 项目想做什么（以及 P0/P1/P2 优先级）
- 当前代码实现到什么程度
- 哪些目录能改，哪些目录不要乱动
- 当前项目适用的代码风格、测试习惯和调试入口

如果只是普通使用者，优先阅读根目录 `README.md`。

## 2. 项目目标与最新优先级

本项目的长期目标是做一个基于 `ESP32-S3` 的学习型通用下载器。

**核心流程：**

1. **学习端**：通过串口代理记录 PC 下载工具与目标芯片之间的下载交互，产出结构化 `JSON` 模板。
2. **回放端**：让 `ESP32` 按模板对目标芯片执行复位、发帧、等待 ACK、重试和下载。

**当前优先级（已更新）：**

| 阶段 | 目标 | 说明 |
|------|------|------|
| **P0** | 串口代理学习 + ESP32 回放 | 当前主线。优先实现 ESP32 串口透传的学习方式，而不是 I2S 被动监听 |
| **P1** | 模板管理、错误恢复 | 回放状态机、复杂 ACK/重试场景、存储稳定性 |
| **P2** | I2S/IIS 被动监听 | 后续增强能力，待 P0 主线稳定后再评估 |

**重要变更说明：**

- I2S 被动监听已降级为 P2，不再是 P0 主线。
- 当前 P0 主线是**串口代理学习方式**：利用 ESP32 已有的 USB CDC ↔ UART 桥接能力，直接记录字节流和控制线，而不是从波形反推协议。
- 这种方式开发周期更短，模板更稳定，更贴近当前代码基础。

项目意图和边界主要来自：

- `agents_doc/01-可行性分析.md`
- `agents_doc/02-方案简述.md`
- `agents_doc/03-UART方案详解.md`
- `agents_doc/04-架构设计文档.md`
- `agents_doc/05-测试文档.md`
- `agents_doc/06-COM代理方案.md`

## 3. 当前实现状态

### 3.1 已落地

- `main/tusb_serial_device_main.c`
  - 当前固件入口
  - 基于 TinyUSB 的多 CDC 串口设备原型
  - 已包含 CDC 到硬件 UART 的基础桥接逻辑（CDC0→UART1, CDC1→UART2）
  - 这是当前串口代理学习的硬件基础

- `tools/com_proxy/`
  - PC 侧工具
  - 负责串口代理、记录 TX/RX/RTS/DTR、匹配 ACK、导出 JSON 模板
  - 当前是项目里最贴近"协议学习"目标的已实现部分

- `source/drivers/Uart_learn/` 和 `source/drivers/IIS_learn/`
  - 刚创建的目录，用于存放驱动实现
  - `Uart_learn/`：UART 学习与回放驱动（待实现）
  - `IIS_learn/`：I2S 被动监听驱动（P2 阶段）

### 3.2 主要仍在设计中

下列模块在架构文档里定义较完整，但当前仓库里尚未按该架构落地：

- `source/drivers/Uart_learn/` 中的具体驱动实现
  - 模板加载和解析
  - 按帧回放 + GPIO 复位控制
  - ACK 接收 + 超时重试逻辑
- `learner`（ESP32 侧学习引擎）
- `player`（回放引擎）
- `storage`（模板存储管理）
- `session_parser`（会话解析器）
- `app_ctrl`（应用层控制）

不要把 `agents_doc/04-架构设计文档.md` 误认为当前代码结构已经全部实现。

## 4. 推荐阅读顺序

新 agent 接手时，建议按下面顺序建立上下文：

1. 读根目录 `README.md`，了解项目对外说明和当前仓库状态。
2. 读 `agents_doc/01-可行性分析.md`，理解为什么当前优先用串口代理而不是 I2S 监听。
3. 读 `agents_doc/02-方案简述.md`，建立整体目标感。
4. 读 `agents_doc/04-架构设计文档.md`，了解规划模块和数据流。
5. 读 `agents_doc/05-测试文档.md`，了解测试策略和未来模块拆分顺序。
6. 看 `main/tusb_serial_device_main.c`，理解当前板端实际能力（CDC 桥接）。
7. 看 `tools/com_proxy/`，理解当前 PC 侧学习工具实现。
8. 查看 `source/drivers/` 目录结构，了解驱动开发预留位置。

## 5. 目录说明

```text
USB_CDC/
├── agents_doc/              # 方案、架构、测试、可行性、COM 代理文档
├── main/                    # 当前 ESP-IDF 固件入口（CDC 桥接原型）
├── source/
│   └── drivers/
│       ├── Uart_learn/      # UART 学习与回放驱动（待实现，P0）
│       └── IIS_learn/         # I2S 被动监听驱动（P2 阶段）
├── tools/com_proxy/         # PC 侧串口代理和模板导出工具
├── drivers/                 # 预留目录，当前基本为空
├── build/                   # 构建输出目录，生成物，不要手工维护
├── managed_components/      # ESP-IDF 管理的组件，不要随意改
├── sdkconfig*               # 工程配置
├── build_esp32.sh           # 构建脚本
├── esp32_download.sh        # 下载脚本
└── pytest_usb_device_serial.py
```

### 目录处理约定

- `build/`、`managed_components/` 这类生成目录默认不要手改。
- **新增驱动实现**优先放在 `source/drivers/<driver_name>/` 下。
- 新工具请放到 `tools/<tool_name>/`。
- 临时调试文件、日志、缓存文件不要长期留在仓库根目录。
- **不要把 I2S 相关实现优先放在 P0 开发**，当前主线是串口代理方式。

## 6. 当前代码风格

项目当前主要有 C 和 Python 两类代码。遵循以下规则。

### 6.1 C 代码规则

- 命名使用有意义的英文
- 函数、变量使用 `snake_case`
- 宏、常量使用 `UPPER_CASE`
- 函数或宏尽量采用"模块_动作"风格，如 `uart_init()`
- 缩进统一 4 个空格，不使用 Tab
- 禁止使用魔法数字，优先使用 `#define` 或 `const`
- 遵循基础 MISRA C 风格，尤其注意：
  - 禁止使用未初始化变量
  - 限制全局变量使用
  - 参数要做合法性检查
- 每个函数应有函数头注释
- 复杂逻辑应补必要说明注释

### 6.2 RTOS 与嵌入式约束

- 任务优先级必须明确，避免多个任务使用相同优先级
- 任务栈大小必须按实际需要设置，不要拍脑袋给默认值
- 任务函数必须是无限循环结构，并在循环中使用 `vTaskDelay()` 或其他非阻塞等待
- 不要在任务中直接访问硬件寄存器，通过驱动或 HAL 层接口访问
- ISR 必须保持短小，复杂逻辑放到任务中处理
- 所有外设初始化都要检查返回值

### 6.3 内存管理规则

- 不使用标准库 `malloc`、`free`
- 需要动态内存时，使用 FreeRTOS 或 ESP-IDF 提供的分配接口
- 所有动态分配必须有释放路径
- 高频分配释放场景优先考虑静态分配或内存池

### 6.4 Python 代码规则

- 优先采用面向对象设计
- 单一职责，高内聚，低耦合
- 配置尽量参数化，不把环境相关信息写死
- 异常处理要统一，不要把错误吞掉
- 入口层只负责参数解析和流程编排，核心逻辑下沉到模块类中

## 7. 当前建议的开发策略

### 7.1 P0 主线：ESP32 串口代理学习 + 回放

当前最优先的工作：

1. **在 `source/drivers/Uart_learn/` 实现 UART 回放驱动**：
   - 模板加载和解析（JSON → 内存结构）
   - GPIO 复位控制（RTS/DTR 或专用复位引脚）
   - 按帧回放：从模板读取 TX 帧，用硬件 UART 发送
   - ACK 接收：等待 RX 应答，超时重试逻辑
   - 帧间隔控制：按模板中的间隔延时

2. **整合到现有 CDC 桥接**：
   - 让 CDC0/CDC1 不仅做桥接，还能切换到"回放模式"
   - 或新增一个 CDC 通道专门用于控制回放流程

3. **模板存储**：
   - 从 SD 卡或 SPI Flash 加载模板
   - 模板格式与 `tools/com_proxy/` 导出的 JSON 保持一致

### 7.2 PC 侧工具优化

在 `tools/com_proxy/` 继续演进：

- 收敛 `JSON` 模板格式，确保与 ESP32 侧解析一致
- 优化 ACK 匹配策略
- 增加原始日志导出
- 评估替换 `com0com` 的跨平台方案，如 `socket` 或 `rfc2217`

### 7.3 I2S 被动监听（P2 阶段）

待 P0 主线稳定后再评估：

- 在 `source/drivers/IIS_learn/` 实现 I2S 采样驱动
- 波特率自动识别算法
- 波形到字节帧的解码
- 与现有模板格式的整合

**不要现在就开始大面积实现 I2S 驱动**，当前性价比低于串口代理路线。

## 8. 调试入口

### 8.1 固件侧

当前固件主入口：

- `main/tusb_serial_device_main.c`

当前构建入口：

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p PORT flash monitor
```

也可以尝试仓库自带脚本：

```bash
./build_esp32.sh
./esp32_download.sh
```

**CDC 桥接测试方法**：

1. 烧录固件后，ESP32 会枚举为 USB CDC 设备
2. 在 PC 上打开 CDC 端口，发送数据
3. 观察数据是否通过 UART1/UART2 转发到目标设备
4. 这是当前可用于串口代理学习的硬件基础

### 8.2 PC 工具侧

当前入口：

```bash
python tools/com_proxy/main.py --help
```

常见依赖：

```bash
pip install pyserial
pip install pytest
```

测试入口：

```bash
python -m pytest tools/com_proxy/tests -q
```

### 8.3 驱动开发调试

新增驱动请在 `source/drivers/<driver_name>/` 下开发：

- 创建 `CMakeLists.txt`
- 创建 `<driver_name>.h` 和 `<driver_name>.c`
- 在 `main/` 中添加测试入口或在 `tests/` 中添加单元测试
- 优先用 Host 侧测试验证逻辑，再上板验证

## 9. 修改代码时的注意事项

- **不要把设计文档里的未来结构直接伪装成已经实现**。
- **优先在 `source/drivers/Uart_learn/` 实现 P0 功能**，而不是分散在 `main/` 里乱改。
- 做文档更新时，要明确标注当前代码状态和规划状态。
- 改动 `JSON` 模板格式时，同步更新：
  - `tools/com_proxy/`
  - `source/drivers/Uart_learn/`（模板解析部分）
  - `README.md`
  - 相关设计文档中涉及模板格式的部分
- 如果新增测试，尽量让 Host 侧测试独立于真实硬件。
- 如果改动固件串口桥接逻辑，注意不要破坏当前可运行的 TinyUSB CDC 原型。
- **不要在 P0 阶段引入 I2S 复杂度**，当前主线是串口代理方式。

## 10. 适合先做的任务

对于新接手的 agent，更适合先从下面这些任务开始：

1. **梳理当前 CDC 桥接逻辑**：理解 `main/tusb_serial_device_main.c` 中 CDC 到 UART 的数据流。
2. **设计 `source/drivers/Uart_learn/` 的驱动接口**：参考 `agents_doc/04-架构设计文档.md` 中的 `player.h` 定义。
3. **实现模板加载和解析**：让 ESP32 能从 SD 卡读取 `tools/com_proxy/` 导出的 JSON 模板。
4. **实现基础回放**：GPIO 复位控制 + UART 按帧发送。
5. **实现 ACK 接收和重试逻辑**：完成闭环测试。
6. **给当前固件原型增加必要注释和模块拆分**，但避免过度重构。

## 11. 不建议直接做的事

- 不要直接大改 `managed_components/`。
- **不要在没有测试或验证路径时一次性引入大量新模块**。
- 不要把 `drivers/`、`source/` 当成临时文件堆放目录。
- **不要在 P0 阶段大面积实现 I2S 驱动**，当前主线是串口代理方式。
- 不要随意引入与当前 UART 主线无关的复杂功能。
- 不要把 Windows 专属假设写死到长期架构里。

## 12. 交接说明

如果未来项目结构发生明显变化，请同步更新这份 `agent_readme.md`，至少覆盖以下几类信息：

- 当前已实现模块
- 当前主入口
- 当前推荐调试方式
- 当前模板格式来源
- 当前代码风格和限制
- **P0/P1/P2 优先级是否有调整**

这份文件的目的不是替代设计文档，而是帮助新 agent 在最短时间内进入正确上下文。
