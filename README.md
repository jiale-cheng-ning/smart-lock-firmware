# Smart Lock Firmware

基于 STM32F411RET6 的智能门锁系统，支持指纹识别、密码开锁和 WiFi 远程通知。

## 功能

- 指纹识别开锁（ZW101 半导体指纹模块）
- 4x4 矩阵键盘密码开锁
- OLED 实时显示系统状态
- ESP8266 WiFi 远程事件通知
- 电磁锁自动控制（开锁 5 秒后自动上锁）
- 连续失败锁定机制（3 次失败锁定 30 秒）

## 硬件

| 模块 | 型号 | 接口 |
|------|------|------|
| 主控 | STM32F411RET6 | - |
| 指纹模块 | ZW101 | USART2 (PA2/PA3) |
| WiFi 模块 | ESP8266 | USART1 (PA9/PA10) |
| 显示屏 | 0.96" OLED | I2C1 (PB6/PB7) |
| 键盘 | 4x4 矩阵键盘 | PB0-PB7 |
| 电磁锁 | 12V 电磁锁 + 继电器 | PA1 |
| 指示灯 | LED | PC13 |

## 目录结构

```
smart-lock-firmware/
├── Core/
│   ├── Src/main.c          # 主程序
│   └── Inc/                 # 头文件
├── Drivers/
│   ├── Fingerprint/         # 指纹模块驱动
│   ├── KeyPad/              # 矩阵键盘驱动
│   ├── OLED/                # OLED 显示驱动
│   ├── Lock/                # 电磁锁控制
│   └── ESP8266/             # WiFi 模块驱动
├── Docs/                    # 文档和原理图
└── Tests/                   # 测试代码
```

## 开发环境

- Keil MDK 5
- STM32F4xx HAL 库
- ST-Link V2 调试器

## 状态机

```
IDLE ──┬── 输入数字 ──→ INPUT_PASSWORD ── 确认 ──→ UNLOCKED
       ├── 按指纹 ──→ VERIFY_FP ──→ UNLOCKED
       └── 3次失败 ──→ LOCKED_OUT (30秒) ──→ IDLE
```

## TODO

- [ ] 实现指纹模块驱动（USART 通信）
- [ ] 实现矩阵键盘扫描
- [ ] 实现 OLED I2C 驱动
- [ ] 实现 ESP8266 AT 指令通信
- [ ] 实现电磁锁继电器控制
- [ ] 添加密码修改功能
- [ ] 添加 WiFi 配网功能
- [ ] 低功耗待机模式
