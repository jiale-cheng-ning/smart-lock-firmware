# Smart Lock Firmware

基于 STM32F411RET6 的智能门锁系统，支持指纹识别、密码开锁和 WiFi 远程通知。

## 功能

- 指纹识别开锁（ZW101 半导体指纹模块）
- 4x4 矩阵键盘密码开锁
- OLED 实时显示系统状态
- ESP8266 WiFi 远程事件通知（开发中）
- 电磁锁自动控制（开锁 5 秒后自动上锁）
- 连续失败锁定机制（3 次失败锁定 30 秒）

## 硬件

| 模块 | 型号 | 接口 | 驱动状态 |
|------|------|------|----------|
| 主控 | STM32F411RET6 | - | - |
| 指纹模块 | ZW101 | USART1 (PA9/PA10) | ✅ 已完成 |
| WiFi 模块 | ESP8266 | USART2 | ⬜ 待开发 |
| 显示屏 | 0.96" OLED | I2C1 (PB6/PB7) | ✅ 已完成 |
| 键盘 | 4x4 矩阵键盘 | PB0-PB7 | ✅ 已完成 |
| 电磁锁 | 12V 电磁锁 + 继电器 | PA1 | ✅ 已完成 |
| 指示灯 | LED | PC13 | - |

## 目录结构

```
smart-lock-firmware/
├── Core/
│   ├── Src/main.c              # 主程序（状态机）
│   └── Inc/main.h              # 引脚定义和配置
├── Drivers/
│   ├── delay.c / delay.h       # SysTick 延时函数
│   ├── KeyPad/
│   │   ├── keypad.c            # 4x4 矩阵键盘驱动
│   │   └── keypad.h
│   ├── OLED/
│   │   ├── oled.c              # SSD1306 OLED 显示驱动（软件 I2C）
│   │   └── oled.h
│   ├── Fingerprint/
│   │   ├── fingerprint.c       # ZW101 指纹模块驱动（USART）
│   │   └── fingerprint.h
│   ├── Lock/
│   │   ├── lock.c              # 电磁锁控制（GPIO + 继电器）
│   │   └── lock.h
│   └── ESP8266/
│       └── esp8266.h           # WiFi 模块接口（待实现）
├── Docs/                       # 文档和原理图
├── Tests/                      # 测试代码
└── README.md
```

## 技术要点

### 矩阵键盘
- GPIO 扫描：4 行（输出） × 4 列（输入），逐行拉低检测
- 按键消抖：检测后延时 15ms 再确认
- 支持阻塞/非阻塞两种扫描模式

### OLED 显示
- 软件模拟 I2C（bit-bang），避免 STM32 硬件 I2C 的已知 bug
- SSD1306 控制器，128×64 像素，8 页 × 128 列
- 支持 ASCII 字符串和数字显示（8×16 字模）

### 指纹模块
- USART1 串口通信，115200 波特率，8N1
- 中断接收，不阻塞 CPU
- ZW101 协议：包头 + 设备地址 + 包标识 + 长度 + 命令 + 数据 + 校验
- 支持：录入指纹（Enroll）、比对搜索（Search 1:N）、删除、清空

### 电磁锁
- GPIO 推挽输出控制继电器模块
- 安全状态设计：默认高电平（锁门），程序崩溃时门保持锁闭

## 开发环境

- Keil MDK 5
- STM32F4xx 标准外设库
- ST-Link V2 调试器
- GCC（本地测试用）

## 状态机

```
IDLE ──┬── 输入数字 ──→ INPUT_PASSWORD ── 确认 ──→ UNLOCKED
       ├── 按指纹 ──→ VERIFY_FP ──→ UNLOCKED
       └── 3次失败 ──→ LOCKED_OUT (30秒) ──→ IDLE
```

## TODO

- [x] 项目骨架和状态机框架
- [x] SysTick 延时函数
- [x] 矩阵键盘驱动（GPIO 扫描 + 消抖）
- [x] OLED 显示驱动（软件 I2C + SSD1306）
- [x] 电磁锁驱动（GPIO + 继电器控制）
- [x] 指纹模块驱动（USART + ZW101 协议）
- [ ] ESP8266 WiFi 驱动（AT 指令通信）
- [ ] 密码修改功能
- [ ] WiFi 配网功能
- [ ] 低功耗待机模式
- [ ] PCB 原理图上传
