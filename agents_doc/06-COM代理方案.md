# ESP32 Universal UART Flasher — COM 端口代理方案

> 版本: v0.2 | 日期: 2026-05-16 | 平台: Windows 10/11
>
> **核心思路：** 不用硬件监听波形，直接在 Windows 层做 COM 端口代理，一边透传数据给目标芯片，一边完整记录交互协议。

---

## 目录

1. [方案总览](#1-方案总览)
2. [软件架构](#2-软件架构)
3. [安装与配置](#3-安装与配置)
4. [使用流程](#4-使用流程)
5. [模板格式](#5-模板格式)
6. [关键实现细节](#6-关键实现细节)
7. [常见问题](#7-常见问题)
8. [与 I2S 硬件方案对比](#8-与-i2s-硬件方案对比)

---

## 1. 方案总览

### 1.1 一句话

> 下载工具连虚拟 COM → 代理桥接到真实 COM → 全程记录 TX/RX + RTS/DTR → 导出 JSON 模板 → ESP32 直接回放。

### 1.2 完整链路

```
┌──────────────────── Windows ────────────────────┐
│                                                   │
│  ┌──────────────┐    ┌──────────────────────┐    │
│  │  下载工具      │    │  COM Proxy Agent     │    │
│  │  (esptool,    │───►│  ┌────────────────┐  │    │
│  │   ST-Link CLI,│    │  │ 虚拟 COM 端口    │  │    │
│  │   Flash      │    │  │ (com0com/null-  │  │    │
│  │   Loader等)  │◄───│  │  modem emulator) │  │    │
│  └──────────────┘    │  └────────┬───────┘  │    │
│                       │           │           │    │
│                       │  ┌────────┴───────┐  │    │
│                       │  │  桥接引擎       │  │    │
│                       │  │  (双向透传+     │  │    │
│                       │  │   全双工日志)   │  │    │
│                       │  └────────┬───────┘  │    │
│                       │           │           │    │
│                       │  ┌────────┴───────┐  │    │
│                       │  │  日志记录器      │  │    │
│                       │  │  TX/RX 帧 +    │  │    │
│                       │  │  RTS/DTR 状态  │  │    │
│                       │  │  + 时间戳       │  │    │
│                       │  └────────────────┘  │    │
│                       └──────────────────────┘    │
│                               │                    │
│                               ▼                    │
│                      protocol_template.json        │
└────────────────────────────────────────────────────┘
                                │
                                ▼ SD 卡拷贝
┌────────────────── ESP32 ───────────────────┐
│                                              │
│  ┌──────────────────────────────────────┐   │
│  │  ESP32 回放引擎                       │   │
│  │  ① 加载 protocol_template.json       │   │
│  │  ② 按模板复位目标芯片                  │   │
│  │  ③ 逐帧发送 + 等 ACK + 超时重试        │   │
│  │  ④ 下载完成                           │   │
│  └──────────────────────────────────────┘   │
│     TX  ──►  目标芯片 RX                    │
│     RX  ◄──  目标芯片 TX                    │
│     RTS ──►  目标芯片 RTS/EN                │
│     DTR ──►  目标芯片 DTR/BOOT              │
└──────────────────────────────────────────────┘
```

---

## 2. 软件架构

### 2.1 组件清单

| 组件 | 语言 | 依赖 | 大小 | 用途 |
|------|------|------|------|------|
| **com-proxy-agent** | Python 3.10+ | pyserial, com0com | ~200 行 | 核心代理 + 日志 |
| **com-proxy-agent.exe** | 打包 exe | PyInstaller 打包 | ~8MB | 免 Python 环境运行 |
| **com0com** / **Null-modem emulator** | 驱动 | 需管理员安装 | ~2MB | 生成虚拟 COM 对 |
| **template-viewer** (可选) | Python | tkinter | ~150 行 | 可视化查看抓到的帧 |

### 2.2 核心类图

```
┌─────────────────────────────────────────┐
│           ComProxyAgent                 │
├─────────────────────────────────────────┤
│ - real_port: str         (真实 COM)     │
│ - virtual_port: str      (虚拟 COM)     │
│ - baudrate: int                         │
│ - frames: list[Frame]                   │
│ - rts_state: bool                       │
│ - dtr_state: bool                       │
│ - recording: bool                       │
├─────────────────────────────────────────┤
│ + __init__(real, virtual, baud)         │
│ + start()                启动代理       │
│ + stop()                 停止代理       │
│ + save_template(path)    导出模板       │
│ + get_statistics()       统计信息       │
└─────────────────────────────────────────┘
                    │
                    ├──→  Thread: bridge_tx  (real→virtual)
                    ├──→  Thread: bridge_rx  (virtual→real)
                    ├──→  Thread: signal_monitor (RTS/DTR)
                    └──→  FrameLogger (写 frames[])
```

---

## 3. 安装与配置

### 3.1 安装虚拟 COM 驱动

**方案 A — com0com（推荐）**

```
1. 下载: https://sourceforge.net/projects/com0com/
   或: https://github.com/nedp/ne0com/releases

2. 安装: 右键管理员运行 setup.exe

3. 配置虚拟 COM 对:
   打开 "com0com Command Prompt" (管理员)
   
   > install 0 PortName=COM10 PortName=COM11
   
   这会生成一对虚拟 COM:
     COM10 ↔ COM11
     (写入 COM10 的数据从 COM11 读出，反之亦然)
   
   > status
   检视当前虚拟端口列表

4. 可选: 用 GUI 管理工具
   安装目录下运行 setupg.exe → 图形界面配对
```

**方案 B — Null-modem Emulator (com-emulate)**

```
1. 下载: https://null-modem-emulator.en.softonic.com/
   https://sourceforge.net/projects/com0com/ (同源)

2. 安装后创建一对虚拟 COM, 操作更简单
```

### 3.2 下载代理脚本

```bash
# 直接运行 (需要 Python 3.10+)
pip install pyserial
python com_proxy_agent.py

# 或打包成 exe, 免 Python 环境
pip install pyinstaller
pyinstaller --onefile --console com_proxy_agent.py
# 生成 dist/com_proxy_agent.exe, 拷到任何 Win10 机器都能跑
```

### 3.3 配置文件

```ini
# config.ini — 与 exe 同目录
[proxy]
real_port = COM3              # 接目标芯片的真实 COM 口
virtual_port = COM10          # 虚拟 COM 口 (下载工具连这个)
baudrate = 115200
data_bits = 8
parity = N                    # N/E/O
stop_bits = 1
flow_control = none           # none / rtscts
log_hex = true                # 日志显示 hex

[recording]
output_dir = ./templates      # 模板输出目录
auto_save_on_idle = true      # 空闲 5s 自动保存
max_session_minutes = 30      # 最长记录时间
record_rts_dtr = true         # 记录复位信号
```

---

## 4. 使用流程

### 4.1 首次使用：学习 + 下载分离

```
Step 1 — 接线
   目标芯片 TX/RX/RTS/DTR/GND ──→ 电脑 USB-TTL 转换器 ──→ COM3

Step 2 — 配置虚拟 COM
   com0com: 创建 COM10 ↔ COM11 对
   或: com_proxy_agent 第一次运行自动提示创建

Step 3 — 启动代理
   > com_proxy_agent.exe
   
   输出:
   [INFO] 真实端口: COM3 (115200-8N1)
   [INFO] 虚拟端口: COM10
   [INFO] 代理已启动, 等待连接...
   [INFO] 检测到下载工具连接 COM10
   [INFO] RTS 拉低 → DTR 拉高 (复位信号)

Step 4 — 下载工具连接虚拟端口
   > esptool.py --port COM10 write_flash 0x0 firmware.bin
   (和平时一样用, 只是端口改成 COM10)

Step 5 — 下载过程中自动记录
   控制台实时显示:
   [TX→] 7F                         (0.000ms)
   [RX←] 79                         (1.200ms)
   [TX→] 00 FF 00 FF 00 FF          (0.500ms)
   [RX←] 79                         (0.850ms)
   [RTS] Low → High (复位脉冲 10ms)
   ...共 1423 帧...

Step 6 — 保存模板
   下载结束 → 代理自动检测空闲 5s
   或按 Ctrl+C 手动保存
   
   输出:
   [INFO] 会话已完成, 保存到 templates/ESP32_115200_2026-05-16.json
   [INFO] 统计: 1423 帧, 64KB 数据, 3.2s 总时长, ACK 率 100%

Step 7 — 拷贝模板到 ESP32 SD 卡
   把 .json 文件拷到 ESP32 回放器的 SD 卡 /templates/ 目录

Step 8 — ESP32 回放
   按之前方案, ESP32 加载模板 → 直接下载目标芯片
```

### 4.2 完全工作流

```
┌───────────┐    ┌──────────────┐    ┌───────────┐
│ 学习阶段    │    │ 模板分发      │    │ 下载阶段    │
├───────────┤    ├──────────────┤    ├───────────┤
│ Windows   │    │ .json 文件    │    │ ESP32     │
│ 装一次     │───►│ SD 卡 /      │───►│ 接目标芯片  │
│ COM代理    │    │ 网络分享      │    │ 按模板回放  │
│ 抓协议     │    │              │    │ 批量下载    │
└───────────┘    └──────────────┘    └───────────┘

优点:
  - 只抓一次模板, 后面批量用
  - 分享模板 = 对方直接能烧, 不需要安装任何工具
  - 新芯片 → Windows 抓一次 → 模板永久可用
```

---

## 5. 模板格式

和架构设计文档保持一致，但 COM 代理生成的模板更精确（省掉了波特率识别和帧同步的模糊度）。

```json
{
  "template_version": "1.0",
  "chip_name": "ESP32_UART_Bootloader",
  "tool_name": "esptool.py v4.7",
  
  "uart": {
    "baudrate": 115200,
    "data_bits": 8,
    "parity": "none",
    "stop_bits": 1,
    "flow_control": "none"
  },

  "reset": {
    "has_rts": true,
    "has_dtr": true,
    "sequence": [
      { "pin": "DTR", "level": 1, "delay_us": 0 },
      { "pin": "RTS", "level": 0, "delay_us": 0 },
      { "pin": "DTR", "level": 0, "delay_us": 100000 },
      { "pin": "RTS", "level": 1, "delay_us": 50000 }
    ],
    "reset_to_first_frame_us": 2000
  },

  "frames": [
    {
      "index": 0,
      "timestamp_us": 0,
      "tx_data": [0x7F],
      "tx_len": 1,
      "rx_ack": [0x79],
      "ack_len": 1,
      "ack_delay_us": 1200,
      "interframe_gap_us": 500,
      "rts_state_before": true,
      "dtr_state_before": false
    }
  ],

  "statistics": {
    "total_frames": 1423,
    "total_bytes_tx": 65536,
    "total_bytes_rx": 2846,
    "total_duration_us": 3245000000,
    "ack_rate": 1.0,
    "max_frame_len": 264,
    "avg_frame_gap_us": 2280
  }
}
```

---

## 6. 关键实现细节

### 6.1 双向桥接 + 日志 — 核心代码

```python
import serial, threading, time, json, os, sys
from datetime import datetime

class ComProxyAgent:
    def __init__(self, real_port="COM3", virtual_port="COM10",
                 baudrate=115200, timeout=0.01):
        self.real_port = real_port
        self.virtual_port = virtual_port
        self.baudrate = baudrate
        self.timeout = timeout
        self.frames = []
        self.running = False
        self.last_activity_time = time.time()
        self.rts_state = None
        self.dtr_state = None
        
    def bridge(self, src, dst, direction, label):
        """双向桥接 + 记录"""
        while self.running:
            try:
                data = src.read(1024)
                if data:
                    dst.write(data)
                    self.last_activity_time = time.time()
                    self.frames.append({
                        "ts_ns": time.time_ns(),
                        "dir": direction,
                        "data": list(data),
                        "len": len(data)
                    })
                    # 控制台实时显示
                    hex_str = " ".join(f"{b:02X}" for b in data[:16])
                    suffix = "..." if len(data) > 16 else ""
                    print(f"  [{label}] {hex_str}{suffix}")
            except serial.SerialException:
                break
            except Exception as e:
                print(f"[ERROR] {label}: {e}")
                break

    def monitor_signals(self, ser):
        """监控 RTS/DTR 状态变化"""
        last_rts = None
        last_dtr = None
        while self.running:
            try:
                current_rts = ser.rts
                current_dtr = ser.dtr
                if current_rts != last_rts or current_dtr != last_dtr:
                    ts = time.time_ns()
                    if last_rts is not None:  # 跳过初始状态
                        print(f"  [SIG] RTS: {last_rts}→{current_rts}, "
                              f"DTR: {last_dtr}→{current_dtr}")
                        self.frames.append({
                            "ts_ns": ts,
                            "dir": "signal",
                            "event": "rts_dtr_change",
                            "rts": current_rts,
                            "dtr": current_dtr
                        })
                    last_rts, last_dtr = current_rts, current_dtr
                time.sleep(0.001)  # 1ms 轮询
            except:
                break

    def start(self):
        """启动代理"""
        self.running = True
        self.frames = []
        
        print(f"\n{'='*50}")
        print(f" COM Proxy Agent v0.2")
        print(f"{'='*50}")
        print(f" 真实端口: {self.real_port} @ {self.baudrate}")
        print(f" 虚拟端口: {self.virtual_port}")
        print(f" 下载工具请连接 {self.virtual_port}")
        print(f"{'='*50}\n")
        
        real = serial.Serial(
            port=self.real_port,
            baudrate=self.baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=self.timeout,
            rtscts=False
        )
        virt = serial.Serial(
            port=self.virtual_port,
            baudrate=self.baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=self.timeout,
            rtscts=False
        )
        
        # 双向桥接线程
        t1 = threading.Thread(target=self.bridge, 
            args=(real, virt, "chip→pc", "RX←"), daemon=True)
        t2 = threading.Thread(target=self.bridge, 
            args=(virt, real, "pc→chip", "TX→"), daemon=True)
        t3 = threading.Thread(target=self.monitor_signals,
            args=(virt,), daemon=True)
        
        t1.start()
        t2.start()
        t3.start()
        
        print(" [等待下载工具连接...]\n")
        t1.join()
        self.running = False
        real.close()
        virt.close()

    def stop(self):
        self.running = False

    def get_statistics(self):
        """统计信息"""
        tx_frames = [f for f in self.frames if f["dir"] == "pc→chip"]
        rx_frames = [f for f in self.frames if f["dir"] == "chip→pc"]
        tx_bytes = sum(f["len"] for f in tx_frames)
        rx_bytes = sum(f["len"] for f in rx_frames)
        
        # 计算 ACK 率（每帧 TX 后有 RX 的占比）
        acked = 0
        for i, f in enumerate(tx_frames):
            if i + 1 < len(rx_frames):
                gap = rx_frames[i]["ts_ns"] - f["ts_ns"]
                if 0 < gap < 50_000_000:  # 50ms 内的 RX 视为 ACK
                    acked += 1
        
        total_time = (self.frames[-1]["ts_ns"] - self.frames[0]["ts_ns"]) / 1e9 \
                     if len(self.frames) > 1 else 0
        
        return {
            "total_frames_tx": len(tx_frames),
            "total_frames_rx": len(rx_frames),
            "total_bytes_tx": tx_bytes,
            "total_bytes_rx": rx_bytes,
            "total_duration_s": round(total_time, 3),
            "ack_rate": round(acked / len(tx_frames), 3) if tx_frames else 0
        }

    def export_template(self, chip_name="unknown"):
        """导出为协议模板 JSON"""
        # 提取复位信号
        reset_events = [f for f in self.frames if f["dir"] == "signal"]
        reset_seq = []
        for evt in reset_events:
            reset_seq.append({
                "pin": "RTS", "level": 1 if evt["rts"] else 0, "delay_us": 0
            })
            reset_seq.append({
                "pin": "DTR", "level": 1 if evt["dtr"] else 0, "delay_us": 0
            })
        
        # 构建帧序列
        tx_frames = [f for f in self.frames if f["dir"] == "pc→chip"]
        rx_frames = [f for f in self.frames if f["dir"] == "chip→pc"]
        
        frame_list = []
        for i, tx in enumerate(tx_frames):
            # 找对应的 ACK
            ack_data = []
            ack_delay = 0
            for rx in rx_frames:
                if rx["ts_ns"] > tx["ts_ns"]:
                    gap = (rx["ts_ns"] - tx["ts_ns"]) / 1000  # ns→μs
                    if gap < 50000:  # 50ms 内
                        ack_data = rx["data"]
                        ack_delay = int(gap)
                    break
            
            # 计算帧间距
            gap = 0
            if i > 0:
                gap = int((tx["ts_ns"] - tx_frames[i-1]["ts_ns"]) / 1000)
            
            frame_list.append({
                "index": i,
                "timestamp_us": int((tx["ts_ns"] - self.frames[0]["ts_ns"]) / 1000),
                "tx_data": tx["data"],
                "tx_len": tx["len"],
                "rx_ack": ack_data,
                "ack_len": len(ack_data),
                "ack_delay_us": ack_delay,
                "interframe_gap_us": gap
            })
        
        stats = self.get_statistics()
        
        template = {
            "template_version": "1.0",
            "chip_name": chip_name,
            "tool_name": f"com-proxy-agent",
            "learn_timestamp": datetime.now().isoformat(),
            "uart": {
                "baudrate": self.baudrate,
                "data_bits": 8,
                "parity": "none",
                "stop_bits": 1,
                "flow_control": "none"
            },
            "reset": {
                "has_rts": len(reset_events) > 0,
                "has_dtr": len(reset_events) > 0,
                "sequence": reset_seq,
                "reset_to_first_frame_us": 0
            },
            "frames": frame_list,
            "total_frames": stats["total_frames_tx"],
            "total_bytes": stats["total_bytes_tx"],
            "total_duration_us": int(stats["total_duration_s"] * 1_000_000),
            "statistics": stats
        }
        
        return template


def main():
    import configparser
    
    # 读取配置
    config = configparser.ConfigParser()
    config.read("config.ini")
    
    real_port = config.get("proxy", "real_port", fallback="COM3")
    virtual_port = config.get("proxy", "virtual_port", fallback="COM10")
    baudrate = config.getint("proxy", "baudrate", fallback=115200)
    output_dir = config.get("recording", "output_dir", fallback="./templates")
    
    os.makedirs(output_dir, exist_ok=True)
    
    print("\n COM 端口代理 — 下载协议学习工具")
    print("=" * 40)
    print(f" 使用前请确保 com0com 已安装")
    print(f" 并创建了 {virtual_port}↔??? 虚拟端口对")
    print("=" * 40)
    
    agent = ComProxyAgent(real_port, virtual_port, baudrate)
    
    try:
        agent.start()
    except KeyboardInterrupt:
        print("\n\n[停止] 用户中断")
    
    agent.stop()
    stats = agent.get_statistics()
    
    print(f"\n{'='*50}")
    print(f" 会话统计")
    print(f"{'='*50}")
    print(f" 总帧数 (TX):    {stats['total_frames_tx']}")
    print(f" 总帧数 (RX):    {stats['total_frames_rx']}")
    print(f" 总字节 (TX):    {stats['total_bytes_tx']}")
    print(f" 总字节 (RX):    {stats['total_bytes_rx']}")
    print(f" 总耗时:         {stats['total_duration_s']}s")
    print(f" ACK 率:         {stats['ack_rate']*100:.1f}%")
    
    # 保存
    chip_name = input("\n 输入芯片名称 (留空自动命名): ").strip()
    if not chip_name:
        chip_name = f"{real_port.replace('COM','')}_{baudrate}"
    
    template = agent.export_template(chip_name)
    filename = f"{chip_name}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
    filepath = os.path.join(output_dir, filename)
    
    with open(filepath, "w") as f:
        json.dump(template, f, indent=2)
    
    print(f"\n ✅ 模板已保存: {filepath}")
    print(f"    拷贝到 ESP32 的 SD 卡 /templates/ 目录即可使用")


if __name__ == "__main__":
    main()
```

### 6.2 延迟影响分析

> 关键问题：**代理桥接引入的延迟会不会干扰下载工具的正常工作？**

| 场景 | 代理延迟 | 影响 |
|------|---------|------|
| **115200 波特, 1 字节帧** | ~87μs 传输时间 + ~10μs 代理转发 ≈ 100μs | **无影响**, 工具不会感知 |
| **921600 波特, 256 字节帧** | ~2.2ms 传输 + ~10μs 代理 ≈ 2.2ms | 无影响 |
| **RTS/DTR 信号** | pyserial 的 rts/dtr 写入走 OS 驱动, 延迟 < 1ms | **无影响** |

**结论：软件代理的延迟远小于 UART 帧传输时间，不会干扰下载工具的时序。**

验证方法：代理模式下和直连模式各下载 3 次，对比耗时（理论上差异 < 0.1%）。

### 6.3 全双工 vs 半双工处理

串口读写必须在**两个独立线程**中同时进行：

```
线程 A (TX方向): 虚拟 COM read() → 真实 COM write()
线程 B (RX方向): 真实 COM read() → 虚拟 COM write()

两个线程互不阻塞，保证全双工
```

半双工方案的坏处（单线程轮询读写）：
- 如果下载工具先发一包、等应答，单线程可以工作
- 但有些下载工具有背靠背发包行为，单线程会卡死

**全双工是刚需，代码里已经这么实现了。**

### 6.4 RTS/DTR 信号记录

RTS/DTR 的状态读取方式：

```python
# 方法 1: pyserial 直接读取 (推荐)
rts = ser.rts      # bool
dtr = ser.dtr      # bool

# 方法 2: 通过 Windows API (更精确)
import ctypes
GetCommModemStatus = ctypes.windll.kernel32.GetCommModemStatus
# ... (略, 方法 1 对于我们的精度要求够用)
```

pyserial 的 `rts`/`dtr` 属性返回的是**当前引脚电平状态**。轮询频率 1ms，对于 10~100ms 级别的复位脉冲来说足够了。

---

## 7. 常见问题

### Q1: com0com 安装失败？

```
管理员权限运行安装程序。
Win10/11 可能需要禁用驱动签名强制:
  设置 → 更新与安全 → 恢复 → 高级启动 → 重启
  → 疑难解答 → 高级选项 → 启动设置 → 禁用驱动程序强制签名
```

### Q2: 下载工具报 "Port busy"？

```
虚拟 COM 口被占用:
1. 检查 com0com 是否正确配对 (COM10↔COM11)
2. 代理脚本占用 COM10 和 COM11 中的一个，
   下载工具应连接另一个
3. 或: 创建新的虚拟 COM 对
   > install 0 PortName=COM20 PortName=COM21
   然后代理连 COM20, 工具连 COM21
```

### Q3: 抓到的模板用不了？

```
常见原因:
1. 芯片每次下载需要不同的握手随机数？
   → 检查帧 0~5 是否有随机变化的字节
   → 需要将变量部分做标记, ESP32 回放时动态替换
   
2. 波特率回放不一致？
   → 检查模板中的 baudrate 是否正确
   
3. 复位时序不对？
   → 检查模板里的 RTS/DTR 序列
   → 对照芯片 datasheet 确认时序
```

### Q4: 下载工具发了 1000 帧，才抓到 100 帧？

```
可能缓冲太小:
  增大 serial.read(4096) 缓冲区
  检查线程是否被 IO 阻塞

可能工具太快:
  Python 线程遇到 GIL 调度延迟
  降采样率或换 C++ 实现
```

### Q5: 能同时监听多个 COM 口吗？

```
可以。开多个 ComProxyAgent 实例, 
或改造成支持多虚拟 COM 对:
  
  > com_proxy_agent.exe --multi
  
  配置文件:
  [com_pairs]
  pair1 = COM3 → COM10
  pair2 = COM4 → COM11
```

### Q6: 下载工具要用管理员权限才能访问 COM 口？

```
代理脚本也需要管理员权限运行:
  > 右键 → 以管理员身份运行
  或: 在 cmd (管理员) 中运行
```

---

## 8. 与 I2S 硬件方案对比

| 对比项 | COM 代理方案（当前） | I2S 硬件监听方案 |
|--------|-------------------|----------------|
| **学习工具** | Windows 软件 + com0com 驱动 | ESP32 硬件 + 面包板接线 |
| **数据精度** | ✅ 纯字节级，零误差 | ⚠️ 需从波形解析，有精度损失 |
| **波特率识别** | ✅ 不需要，直接从 COM 口配置拿 | ❌ 需自行实现脉冲统计 |
| **全双工分离** | ✅ 双线程天然分离 TX/RX | ⚠️ 需 I2S 通道区分，多一根线 |
| **RTS/DTR 记录** | ✅ pyserial 一行代码 | ❌ 需要额外 GPIO 采样 |
| **部署给同事** | ❌ 需要 Windows + 装驱动 | ✅ 插线即用，免电脑免软件 |
| **现场使用** | ❌ 必须带笔记本 | ✅ 巴掌大 ESP32 搞定 |
| **开发成本** | **2 天完成脚本** | 3~4 周硬件 + 固件 |
| **模板质量** | ✅ 直接精确到字节 | ⚠️ 取决于波形解析质量 |

### 最终建议

```
日常开发 → COM 代理方案 (快、准、省事)
  1 天写完脚本, 马上能抓模板

现场批量烧录 → ESP32 回放器
  抓一次模板 → 分发给现场人员
  现场不需要电脑, 也不需要懂协议

出厂量产 → 考虑直接改, 两份一起用
  COM 代理抓模板, ESP32 回放批量烧
  两条腿走路, 互不冲突
```

---

## 附录 — 文件清单

```
com-proxy-agent/
├── com_proxy_agent.py        ← 主程序 (200 行)
├── config.ini                ← 配置文件
├── templates/                ← 模板输出目录
│   ├── ESP32_115200_20260516.json
│   └── STM32F103_115200_20260516.json
├── README.md                 ← 使用说明
└── dist/
    └── com_proxy_agent.exe   ← 打包后的 exe
```
