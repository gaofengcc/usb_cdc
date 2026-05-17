# COM Proxy 工具说明

## 1. 工具用途

`com_proxy` 用于在 Windows 上代理虚拟串口和真实串口之间的通信, 并把下载工具与目标芯片之间的交互过程记录为 `JSON` 模板.

这个模板后续可以给 `ESP32` 回放端使用, 让设备脱离 PC 独立执行相同的下载流程.

当前工具支持记录以下内容:

- PC 到目标芯片的下行数据.
- 目标芯片到 PC 的上行数据.
- `RTS` 和 `DTR` 状态变化.
- 帧间隔和 ACK 延迟.
- 会话统计信息.

## 2. 目录结构

```text
tools/com_proxy/
├── README.md
├── main.py              # 启动入口
├── com_proxy.ini        # 默认配置
├── __init__.py
├── agent.py             # 串口桥接和采集逻辑
├── config.py            # 配置加载和校验
├── models.py            # 数据结构
├── template_builder.py  # JSON 模板生成
└── tests/
    ├── conftest.py
    └── test_template_builder.py
```

## 3. 运行环境

建议环境:

- Windows 10 或 Windows 11.
- Python 3.10 及以上.
- 已安装虚拟串口驱动, 如 `com0com`.
- 已安装 `pyserial`.

安装 Python 依赖:

```bash
pip install pyserial
```

如果需要运行测试, 还需要安装:

```bash
pip install pytest
```

## 4. 使用前准备

### 4.1 创建虚拟串口对

需要先创建一对虚拟串口, 例如:

- `COM10`
- `COM11`

这两个端口会互相连通. 实际使用时, 下载工具连接其中一个, 代理程序连接另一个.

### 4.2 接线关系

推荐连接关系如下:

```text
下载工具 <-> 虚拟 COMA
虚拟 COMB <-> com_proxy
com_proxy <-> 真实 COM <-> USB 转串口 <-> 目标芯片
```

例如:

- 真实端口: `COM3`
- 虚拟端口: `COM10`
- 与 `COM10` 成对的另一个虚拟端口给下载工具使用

注意:

- `real_port` 和 `virtual_port` 不能相同.
- 如果下载工具提示端口被占用, 先检查虚拟串口配对关系是否正确.

## 5. 配置文件

默认配置文件是:

```text
tools/com_proxy/com_proxy.ini
```

配置示例:

```ini
[proxy]
real_port = COM3
virtual_port = COM10
baudrate = 115200
data_bits = 8
parity = N
stop_bits = 1
timeout_s = 0.01
flow_control = none

[recording]
output_dir = tools/com_proxy/templates
auto_save_on_idle = true
idle_timeout_s = 5.0
max_session_minutes = 30
record_rts_dtr = true
ack_timeout_ms = 50
signal_poll_interval_s = 0.001
read_chunk_size = 1024
```

关键字段说明:

- `real_port`: 连接目标芯片的真实串口.
- `virtual_port`: 代理程序占用的虚拟串口.
- `output_dir`: 模板输出目录.
- `auto_save_on_idle`: 空闲超时后自动结束采集.
- `idle_timeout_s`: 空闲多久后自动结束.
- `ack_timeout_ms`: ACK 配对时间窗口.
- `record_rts_dtr`: 是否记录复位相关信号.

## 6. 启动方法

在项目根目录运行:

```bash
python tools/com_proxy/main.py
```

也可以指定参数覆盖配置:

```bash
python tools/com_proxy/main.py --real-port COM3 --virtual-port COM10 --baudrate 115200 --chip-name ESP32_UART_Boot
```

查看帮助:

```bash
python tools/com_proxy/main.py --help
```

## 7. 典型使用流程

### 7.1 启动代理

```bash
python tools/com_proxy/main.py
```

启动后会显示当前使用的真实串口, 虚拟串口和输出目录.

### 7.2 让下载工具连接虚拟串口

例如把下载工具端口改成与代理工作链路对应的虚拟串口.

示例:

```bash
esptool.py --port COM11 write_flash 0x0 firmware.bin
```

这里需要根据你的虚拟串口配对关系决定到底是 `COM10` 还是 `COM11`.

### 7.3 结束采集

有两种结束方式:

- 用户按 `Ctrl+C` 主动停止.
- 会话空闲超过 `idle_timeout_s` 后自动停止.

停止后工具会自动生成一份 `JSON` 模板.

## 8. 输出结果

模板默认输出到:

```text
tools/com_proxy/templates/
```

文件名格式类似:

```text
ESP32_UART_Boot_20260517_231500.json
```

生成的 JSON 中包含:

- UART 参数.
- 复位序列.
- 下行帧列表.
- 匹配到的 ACK.
- 帧间隔.
- 总帧数和总字节数.
- ACK 命中率.

## 9. 测试

当前提供的是不依赖真实串口的基础测试, 主要验证:

- ACK 配对逻辑.
- 复位序列导出逻辑.

运行方式:

```bash
python -m pytest tools/com_proxy/tests -q
```

如果本机没有安装 `pytest`, 需要先安装.

## 10. 常见问题

### 10.1 提示找不到 `serial` 模块

说明没有安装 `pyserial`.

执行:

```bash
pip install pyserial
```

### 10.2 下载工具无法连接端口

优先检查:

- 虚拟串口是否成对创建成功.
- 下载工具是否连接到了正确的那个虚拟端口.
- 代理程序是否已经占用了另一个虚拟端口.
- 是否有其他串口工具占用了真实端口.

### 10.3 没有生成 JSON

可能原因:

- 代理期间没有捕获到任何收发数据.
- 串口配置错误, 如波特率不一致.
- 下载工具连接错了端口.

### 10.4 抓到了数据, 但模板后续不可用

常见原因:

- `RTS` 或 `DTR` 复位链路没有记录完整.
- 目标芯片协议中有动态字段.
- ACK 配对窗口太短或太长.
- 实际烧录流程包含多段重试会话, 需要后续继续增强模板筛选策略.

## 11. 后续扩展建议

当前版本已经可以作为第一版协议学习工具使用, 后续可以继续扩展:

- 原始会话日志导出.
- 多段会话筛选.
- 更精确的 ACK 配对策略.
- GUI 查看器.
- 生成更贴近 ESP32 回放端的数据格式.
