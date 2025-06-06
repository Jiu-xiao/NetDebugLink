# NetDebugLink

**NetDebugLink** 是一款基于 ESP32-C3 的多串口桥接模块，支持通过 WiFi 实现远程调试、日志查看和控制功能。该项目提供了一键烧录功能，并通过 GitHub Actions 自动构建和发布固件。

本项目基于 [LibXR](https://github.com/Jiu-xiao/libxr) 跨平台嵌入式框架构建，支持远程配置和控制，适用于各种调试场景。

---

## 🔧 功能特点

- 通过 BLUFI 协议远程配置 WiFi
- 支持多串口并行工作，便于调试多个设备
- USB CDC 支持，通过 USB 串口与 Linux 主机进行通信
- 通过 WiFi 实现远程调试、日志查看与控制
- 自动识别接入的 ESP32 设备，自动配置并控制
- GitHub Actions 自动构建并发布固件

---

## 🚀 快速开始

### 一键烧录 NetDebugLink 固件

点击下方按钮，使用 Espressif 官方的 [ESP Launchpad](https://espressif.github.io/esp-launchpad) 工具，通过浏览器直接将固件直接烧录到 ESP32-C3 设备上，操作无需任何额外配置。请确保您的设备已通过 USB 串口连接。

[![Try with ESP Launchpad](https://espressif.github.io/esp-launchpad/assets/try_with_launchpad.png)](https://jiu-xiao.github.io/NetDebugLink/esp_launchpad/?flashConfigURL=https://raw.githubusercontent.com/Jiu-xiao/NetDebugLink/master/esp_launchpad.toml)

---

## 🛠️ 本地构建方法

### 1. 使用 Docker 构建项目

使用 GitHub 提供的 Docker 镜像 **`ghcr.io/xrobot-org/docker-image-esp32:main`** 来进行构建。运行以下命令启动 Docker 容器并进入容器内部：

```bash
docker run --rm -it -v $(pwd):/workspace ghcr.io/xrobot-org/docker-image-esp32:main /bin/bash
```

### 2. 克隆项目和 `libxr`

```bash
pip install --upgrade pip
pip install xrobot
git clone https://github.com/Jiu-xiao/NetDebugLink.git
cd NetDebugLink
git clone https://github.com/Jiu-xiao/libxr.git
xrobot_setup
```

### 3. 编译可执行文件

```bash
source ~/esp/esp-idf/export.sh
idf.py build
```

### 4. 拼接 ESP32-C3 固件

```bash
python3 -m esptool --chip esp32c3 merge_bin \
            -o build/NetDebugLink_Firmware.bin \
            --flash_mode dio \
            --flash_freq 80m \
            --flash_size 2MB \
            0x0 build/bootloader/bootloader.bin \
            0x8000 build/partition_table/partition-table.bin \
            0x10000 build/NetDebugLink.bin
ls -l build/NetDebugLink_Firmware.bin
```

### 5. 烧录固件

烧录`build/NetDebugLink_Firmware.bin`到 ESP32-C3 设备，建议使用 Espressif 官方的 [ESP Launchpad](https://espressif.github.io/esp-launchpad) 工具，在浏览器直接将固件直接烧录到设备上，操作无需任何额外配置。

---

## 🧪 示例用法

安装并配置好固件后，通过 USB 连接 ESP32-C3 后，程序会自动识别串口并：

- 通过 WiFi 自动连接并配置网络
- 启动远程调试终端，通过网络发送命令（如 REBOOT、PING）
- 支持命令行接口操作，便于与 ESP32 设备交互

---

## 📌 ESP32-C3 引脚连接

```txt
                   +----------------------+
                   |       ESP32-C3       |
                   |      +--------+      |
  3.3V         --->| 3V3  |        |  GND |<--- GND
  UART1 TX     --->| IO3  |        |  IO4 |<--- UART1 RX
  UART2 TX     --->| IO5  |        |  IO6 |<--- UART2 RX
  LED (PWM)    --->| IO8  |        |      |
  按钮（配网）   --->| IO9  |        |      |
  CDC D+ (USB) <---| IO19 |        | IO18 |---> CDC D- (USB)
                   |      +--------+      |
                   +----------------------+
```

- **CDC USB 口（原生 USB）**
  - `IO19`：USB D+，连接 USB 主机
  - `IO18`：USB D-，连接 USB 主机

- **串口 1（UART0）**
  - `IO3`：TX，连接外设 RX（如另一个 MCU）
  - `IO4`：RX，连接外设 TX

- **串口 2（UART1）**
  - `IO5`：TX
  - `IO6`：RX

- **LED 指示灯**
  - `IO8`：PWM 控制

- **配网按钮**
  - `IO9`：GPIO 输入，默认高电平，按下拉低触发配网（BLUFI）

## 💡 LED 状态指示说明

LED 通过 PWM 控制亮灭频率与亮度，不同的连接状态对应不同的闪烁效果：

| 模式           | 描述           | PWM 频率 | 占空比 | 显示效果       |
| -------------- | -------------- | -------- | ------ | -------------- |
| `Init`         | 初始状态       | 10 Hz    | 50%    | 快速闪烁       |
| `SMART_CONFIG` | 正在进行配网   | 10 Hz    | 50%    | 快速闪烁       |
| `SCANING`      | 等待客户端连接 | 4 Hz     | 75%    | 中速闪烁，偏亮 |
| `CONNECTED`    | 已连接客户端   | 2 Hz     | 25%    | 慢速闪烁，偏暗 |

## 📁 目录结构

```bash
NetDebugLink/
├── build/                    # 构建输出目录（由 CMake/ESP-IDF 自动生成）
│   ├── bootloader/
│   │   └── bootloader.bin    # ESP32-C3 Bootloader 固件
│   ├── partition_table/
│   │   └── partition-table.bin # ESP32-C3 Partition Table 固件
│   └── NetDebugLink.elf      # ESP32-C3 固件
├── CMakeLists.txt            # 顶层 CMake 构建配置文件
├── config.env                # 构建环境配置（可选）
├── esp_launchpad/            # 网页端启动器（用于 ESP32 配网或控制）
├── esp_launchpad.toml        # 启动器元信息配置
├── libxr/                    # LibXR 库
├── LICENSE                   # 许可证
├── Modules/                  # 功能模块目录
│   ├── BlinkLED/             # 控制 LED 闪烁的模块
│   ├── NetDebugLink/         # 主功能模块：WiFi、串口桥接、配网等
│   └── CMakeLists.txt        # 模块聚合配置
├── README.md                 # 项目介绍文档
├── sdkconfig                 # ESP-IDF 生成的配置文件
└── User/                     # 用户代码入口
    ├── CMakeLists.txt        # 用户代码构建配置
    ├── main.cpp              # 项目主函数
    ├── xrobot_main.hpp       # 用户主逻辑头文件
    └── xrobot.yaml           # XRobot 模块/配置描述
```

---

## 📄 License

MIT License © 2025 Jiu-xiao
