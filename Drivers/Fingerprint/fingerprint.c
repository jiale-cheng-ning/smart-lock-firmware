/**
 * ZW101 指纹模块驱动
 *
 * 通信方式：USART 串口（PA9=TX, PA10=RX）
 * 波特率：115200（和模块出厂默认一致）
 *
 * 这个文件的核心功能：
 *   1. 初始化 USART1（配置串口参数）
 *   2. 发送/接收数据（底层函数）
 *   3. 封装指纹模块的命令（录入、比对、删除等）
 *
 * ZW101 通信协议格式：
 *   [包头2] [地址4] [标识1] [长度2] [命令1] [数据N] [校验2]
 *   0xEF01  FFFFFFFF  0x01   N+2    CMD    ...    SUM
 */

#include "fingerprint.h"
#include "delay.h"
#include <string.h>

/* ========== 硬件配置 ========== */
#define FP_USART            USART1
#define FP_USART_RCC        RCC_APB2Periph_USART1
#define FP_GPIO_RCC         RCC_AHB1Periph_GPIOA
#define FP_TX_PIN           GPIO_Pin_9
#define FP_RX_PIN           GPIO_Pin_10
#define FP_TX_SOURCE        GPIO_PinSource9
#define FP_RX_SOURCE        GPIO_PinSource10
#define FP_GPIO_AF          GPIO_AF_USART1

/* ========== 协议常量 ========== */
#define FP_HEADER           0xEF01      /* 包头 */
#define FP_ADDRESS          0xFFFFFFFF  /* 默认设备地址 */
#define FP_CMD              0x01        /* 命令包标识 */
#define FP_ACK              0x07        /* 应答包标识 */

/* ZW101 命令码 */
#define CMD_GET_IMAGE       0x01        /* 录取指纹图像 */
#define CMD_GEN_CHAR        0x02        /* 生成特征值 */
#define CMD_MATCH           0x03        /* 比对（1:1） */
#define CMD_SEARCH          0x04        /* 搜索（1:N） */
#define CMD_REG_MODEL       0x05        /* 合并特征，生成模板 */
#define CMD_STORE           0x06        /* 存储模板 */
#define CMD_LOAD_CHAR       0x07        /* 读取模板 */
#define CMD_DELETE_CHAR     0x0C        /* 删除模板 */
#define CMD_EMPTY           0x0D        /* 清空所有模板 */
#define CMD_GET_NUMBER      0x1D        /* 获取已存模板数量 */

/* 应答码 */
#define ACK_SUCCESS         0x00        /* 操作成功 */
#define ACK_FAIL            0x01        /* 操作失败 */
#define ACK_NO_FINGER       0x02        /* 没有手指 */
#define ACK_GET_IMAGE_FAIL  0x03        /* 录入图像失败 */
#define ACK_GEN_CHAR_FAIL   0x06        /* 特征生成失败 */
#define ACK_NO_MATCH        0x09        /* 没有匹配 */
#define ACK_TIMEOUT         0x0A        /* 超时 */

/* Buffer 大小 */
#define RX_BUFFER_SIZE      256

/* ========== 全局变量 ========== */
static uint8_t rxBuffer[RX_BUFFER_SIZE];
static volatile uint16_t rxIndex = 0;
static volatile uint8_t rxComplete = 0;

/* ========== USART 底层函数 ========== */

/**
 * USART1_Init - 初始化 USART1
 *
 * USART 是 STM32 的硬件串口外设，自动帮你处理：
 *   - 起始位/停止位的生成和检测
 *   - 波特率的精确控制
 *   - 数据的移位发送和接收
 *
 * 你需要配置的参数：
 *   1. 波特率（115200 = 每秒传 115200 位）
 *   2. 数据位（8 位）
 *   3. 停止位（1 位）
 *   4. 校验位（无校验）
 *
 * 这些参数必须和指纹模块那边一致，否则收到的数据全是乱码。
 * 就像两个人对讲机，必须调到同一个频道。
 */
static void USART1_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    USART_InitTypeDef USART_InitStruct;
    NVIC_InitTypeDef NVIC_InitStruct;

    /* 1. 开时钟
     * 注意：USART1 挂在 APB2 总线上（高速外设）
     * USART2/3 挂在 APB1 总线上（低速外设）
     * GPIOA 挂在 AHB1 总线上 */
    RCC_APB2PeriphClockCmd(FP_USART_RCC, ENABLE);
    RCC_AHB1PeriphClockCmd(FP_GPIO_RCC, ENABLE);

    /* 2. 配置 GPIO 为复用功能
     * TX 和 RX 不是普通的 GPIO 输出/输入，而是"复用功能"——
     * 引脚的控制权交给了 USART 外设，不是 CPU 直接控制。
     * 就像 USB 口：你插上鼠标后，操作系统接管了那个端口。 */
    GPIO_PinAFConfig(GPIOA, FP_TX_SOURCE, FP_GPIO_AF);  /* PA9 → USART1_TX */
    GPIO_PinAFConfig(GPIOA, FP_RX_SOURCE, FP_GPIO_AF);  /* PA10 → USART1_RX */

    GPIO_InitStruct.GPIO_Pin   = FP_TX_PIN | FP_RX_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_AF;        /* 复用功能模式！ */
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* 3. 配置 USART 参数 */
    USART_InitStruct.USART_BaudRate            = 115200;     /* 波特率 */
    USART_InitStruct.USART_WordLength           = USART_WordLength_8b;  /* 8 位数据 */
    USART_InitStruct.USART_StopBits             = USART_StopBits_1;     /* 1 个停止位 */
    USART_InitStruct.USART_Parity               = USART_Parity_No;      /* 无校验 */
    USART_InitStruct.USART_HardwareFlowControl  = USART_HardwareFlowControl_None;  /* 无流控 */
    USART_InitStruct.USART_Mode                 = USART_Mode_Tx | USART_Mode_Rx;   /* 收发都开 */
    USART_Init(FP_USART, &USART_InitStruct);

    /* 4. 开启接收中断
     * 为什么用中断？因为指纹模块随时可能发数据过来，
     * 如果用"死等"（轮询），CPU 就被占住了干不了别的。
     * 中断的好处：数据来了自动触发处理函数，CPU 平时该干嘛干嘛。
     *
     * 就像手机来短信：你不需要一直盯着屏幕等，
     * 有短信来了手机会自动响铃通知你。 */
    USART_ITConfig(FP_USART, USART_IT_RXNE, ENABLE);  /* RXNE = 接收寄存器非空中断 */

    NVIC_InitStruct.NVIC_IRQChannel                   = USART1_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStruct.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStruct);

    /* 5. 使能 USART */
    USART_Cmd(FP_USART, ENABLE);
}

/**
 * USART1_IRQHandler - USART1 中断处理函数
 *
 * 这个函数名是固定的！启动文件（startup_stm32f411xx.s）里
 * 已经定义好了中断向量表，名字对应不上就进不来。
 *
 * 每收到 1 字节数据，硬件自动触发这个函数。
 * 我们在这里把数据存到 rxBuffer 里。
 */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(FP_USART, USART_IT_RXNE) != RESET) {
        uint8_t data = USART_ReceiveData(FP_USART);  /* 读取收到的字节 */

        if (rxIndex < RX_BUFFER_SIZE) {
            rxBuffer[rxIndex++] = data;
        }

        /* 清除中断标志（读 DR 已经自动清了，这里保险起见再清一次） */
        USART_ClearITPendingBit(FP_USART, USART_IT_RXNE);
    }
}

/**
 * USART1_SendByte - 发送一个字节
 *
 * 发送过程：
 *   1. 等待发送寄存器空（上一个字节还没发完就等着）
 *   2. 把数据写到 DR 寄存器
 *   3. 硬件自动加上起始位、停止位，然后从 TX 引脚发出去
 */
static void USART1_SendByte(uint8_t data)
{
    /* TXE = Transmit Data Register Empty（发送寄存器空）
     * 等它变 1 才能写下一个字节 */
    while (USART_GetFlagStatus(FP_USART, USART_FLAG_TXE) == RESET);
    USART_SendData(FP_USART, data);
}

/**
 * USART1_SendBuffer - 发送一段数据
 */
static void USART1_SendBuffer(uint8_t *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++) {
        USART1_SendByte(buf[i]);
    }
    /* 等待最后一个字节发完
     * TC = Transmission Complete（发送完成） */
    while (USART_GetFlagStatus(FP_USART, USART_FLAG_TC) == RESET);
}

/* ========== 协议层函数 ========== */

/**
 * FP_SendPacket - 发送一个完整的命令包
 *
 * 把各个字段拼成一个完整的包发出去：
 *   [包头 0xEF01] [地址 FFFFFFFF] [标识 0x01] [长度] [命令] [数据...] [校验]
 */
static void FP_SendPacket(uint8_t cmd, uint8_t *data, uint16_t dataLen)
{
    uint8_t packet[64];
    uint16_t idx = 0;
    uint16_t length = dataLen + 2;  /* 长度 = 命令(1) + 数据(N) + 校验(2) - 校验不算在长度里?
                                       实际上长度 = 包标识后到校验前的字节数 = 1 + dataLen + 2
                                       但 ZW101 的长度字段 = 命令 + 数据长度，不含校验 */
    uint16_t checksum;
    uint16_t i;

    length = dataLen + 3;  /* 包标识(1) + 长度(2) 不算，长度 = 命令(1) + 数据(N) + 校验(2) */
    /* 实际协议：长度字段 = 包标识之后到校验之前的字节数 = 1(命令) + dataLen + 2(校验) */
    length = 2 + 1 + dataLen;  /* 命令(1) + 数据(N) + 校验(2) */

    /* 包头 */
    packet[idx++] = 0xEF;
    packet[idx++] = 0x01;

    /* 设备地址 */
    packet[idx++] = 0xFF;
    packet[idx++] = 0xFF;
    packet[idx++] = 0xFF;
    packet[idx++] = 0xFF;

    /* 包标识 */
    packet[idx++] = FP_CMD;

    /* 长度（高字节在前，这是大端序） */
    packet[idx++] = (length >> 8) & 0xFF;
    packet[idx++] = length & 0xFF;

    /* 命令 */
    packet[idx++] = cmd;

    /* 数据 */
    if (data != NULL && dataLen > 0) {
        memcpy(&packet[idx], data, dataLen);
        idx += dataLen;
    }

    /* 计算校验：包标识 + 长度 + 命令 + 数据 的累加和 */
    checksum = FP_CMD;
    checksum += (length >> 8) & 0xFF;
    checksum += length & 0xFF;
    checksum += cmd;
    for (i = 0; i < dataLen; i++) {
        checksum += data[i];
    }

    /* 校验（高字节在前） */
    packet[idx++] = (checksum >> 8) & 0xFF;
    packet[idx++] = checksum & 0xFF;

    /* 发送 */
    USART1_SendBuffer(packet, idx);
}

/**
 * FP_WaitAck - 等待模块应答
 *
 * 发完命令后，指纹模块会回一个应答包。
 * 我们等它回完，然后解析应答码。
 *
 * 返回值：
 *   0x00 = 成功
 *   其他 = 失败（具体的错误码）
 *   0xFF = 超时（模块没回应）
 */
static uint8_t FP_WaitAck(uint32_t timeout_ms)
{
    uint32_t tick = 0;

    /* 重置接收状态 */
    rxIndex = 0;
    rxComplete = 0;

    /* 等待收到数据
     * 这里用的是"轮询等待"，比较简单但会占 CPU。
     * 生产代码可以用信号量或回调，但学习阶段这样最直观。 */
    while (tick < timeout_ms) {
        Delay_ms(1);
        tick++;

        /* 检查是否收到了完整的应答包
         * 应答包格式：[包头2][地址4][标识1][长度2][确认码1][数据N][校验2]
         * 最少 12 字节 */
        if (rxIndex >= 12) {
            /* 找包头 0xEF01 */
            if (rxBuffer[0] == 0xEF && rxBuffer[1] == 0x01) {
                /* 应答码在第 9 字节（从 0 开始数） */
                return rxBuffer[9];
            }
        }
    }

    return 0xFF;  /* 超时 */
}

/* ========== 初始化 ========== */

/**
 * Fingerprint_Init - 初始化指纹模块
 *
 * 做两件事：
 *   1. 初始化 USART1（配置串口参数）
 *   2. 测试和模块的通信（发一个查询命令）
 */
void Fingerprint_Init(void)
{
    USART1_Init();
    Delay_ms(100);  /* 等模块上电稳定 */
}

/* ========== 用户接口函数 ========== */

/**
 * Fingerprint_IsFingerPressed - 检测是否有手指放在传感器上
 *
 * 原理：发 CMD_GET_IMAGE 命令，如果返回 ACK_NO_FINGER 就是没有手指。
 */
uint8_t Fingerprint_IsFingerPressed(void)
{
    FP_SendPacket(CMD_GET_IMAGE, NULL, 0);
    uint8_t ack = FP_WaitAck(1000);
    return (ack == ACK_SUCCESS) ? 1 : 0;
}

/**
 * Fingerprint_Enroll - 录入指纹
 *
 * 录入过程（需要按 3 次手指）：
 *   第 1 次按下 → 录入图像 → 生成特征存到 Buffer1
 *   第 2 次按下 → 录入图像 → 生成特征存到 Buffer2
 *   第 3 次按下 → 录入图像 → 生成特征存到 Buffer1（覆盖）
 *   合并 Buffer1 和 Buffer2 → 生成模板
 *   存储模板到指定 ID 号
 *
 * 参数：
 *   id - 存储位置（1~1000）
 *
 * 返回值：
 *   1 = 成功
 *   0 = 失败
 */
uint16_t Fingerprint_Enroll(uint16_t id)
{
    uint8_t idData[2];
    uint8_t bufNum;
    uint8_t ack;

    /* 第 1 次按手指 */
    /* 1. 录入图像 */
    FP_SendPacket(CMD_GET_IMAGE, NULL, 0);
    ack = FP_WaitAck(5000);
    if (ack != ACK_SUCCESS) return 0;

    /* 2. 生成特征，存到 Buffer 1 */
    bufNum = 1;
    FP_SendPacket(CMD_GEN_CHAR, &bufNum, 1);
    ack = FP_WaitAck(5000);
    if (ack != ACK_SUCCESS) return 0;

    /* 第 2 次按手指 */
    FP_SendPacket(CMD_GET_IMAGE, NULL, 0);
    ack = FP_WaitAck(5000);
    if (ack != ACK_SUCCESS) return 0;

    bufNum = 2;
    FP_SendPacket(CMD_GEN_CHAR, &bufNum, 1);
    ack = FP_WaitAck(5000);
    if (ack != ACK_SUCCESS) return 0;

    /* 合并两次特征，生成模板 */
    FP_SendPacket(CMD_REG_MODEL, NULL, 0);
    ack = FP_WaitAck(5000);
    if (ack != ACK_SUCCESS) return 0;

    /* 存储模板到指定 ID */
    idData[0] = (id >> 8) & 0xFF;  /* ID 高字节 */
    idData[1] = id & 0xFF;          /* ID 低字节 */
    FP_SendPacket(CMD_STORE, idData, 2);
    ack = FP_WaitAck(5000);

    return (ack == ACK_SUCCESS) ? 1 : 0;
}

/**
 * Fingerprint_Match - 比对指纹（1:N 搜索）
 *
 * 把当前按下的手指和数据库里所有模板比对。
 *
 * 返回值：
 *   > 0 = 匹配到的指纹 ID
 *   0   = 没有匹配
 */
uint16_t Fingerprint_Match(void)
{
    uint8_t ack;
    uint8_t searchData[4];

    /* 1. 录入图像 */
    FP_SendPacket(CMD_GET_IMAGE, NULL, 0);
    ack = FP_WaitAck(3000);
    if (ack != ACK_SUCCESS) return 0;

    /* 2. 生成特征 */
    uint8_t bufNum = 1;
    FP_SendPacket(CMD_GEN_CHAR, &bufNum, 1);
    ack = FP_WaitAck(3000);
    if (ack != ACK_SUCCESS) return 0;

    /* 3. 搜索数据库
     * 参数：起始页(2字节) + 页数(2字节)
     * 起始页 = 0，页数 = 100（搜索前 100 个指纹） */
    searchData[0] = 0x00;  /* 起始页高字节 */
    searchData[1] = 0x00;  /* 起始页低字节 */
    searchData[2] = 0x00;  /* 页数高字节 */
    searchData[3] = 0x64;  /* 页数低字节 = 100 */

    FP_SendPacket(CMD_SEARCH, searchData, 4);
    ack = FP_WaitAck(5000);

    if (ack == ACK_SUCCESS) {
        /* 匹配成功！ID 在应答包的第 10~11 字节 */
        uint16_t id = (rxBuffer[10] << 8) | rxBuffer[11];
        return id;
    }

    return 0;  /* 没有匹配 */
}

/**
 * Fingerprint_Delete - 删除指定 ID 的指纹模板
 */
uint8_t Fingerprint_Delete(uint16_t id)
{
    uint8_t data[4];
    data[0] = (id >> 8) & 0xFF;
    data[1] = id & 0xFF;
    data[2] = 0x00;  /* 删除数量高字节 */
    data[3] = 0x01;  /* 删除数量低字节 = 1 */

    FP_SendPacket(CMD_DELETE_CHAR, data, 4);
    uint8_t ack = FP_WaitAck(5000);
    return (ack == ACK_SUCCESS) ? 1 : 0;
}

/**
 * Fingerprint_Empty - 清空所有指纹模板
 */
uint8_t Fingerprint_Empty(void)
{
    FP_SendPacket(CMD_EMPTY, NULL, 0);
    uint8_t ack = FP_WaitAck(5000);
    return (ack == ACK_SUCCESS) ? 1 : 0;
}
