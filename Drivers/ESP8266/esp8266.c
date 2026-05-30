/**
 * ESP8266 WiFi 模块驱动
 *
 * 通信方式：USART2 串口（PA2=TX, PA3=RX）
 * 波特率：115200（ESP8266 默认出厂设置）
 * 控制方式：AT 指令（文本命令）
 *
 * AT 指令就像和模块"对话"：
 *   你发 "AT\r\n"，它回 "OK\r\n" —— 连接正常
 *   你发 "AT+CWJAP=\"WiFi\",\"密码\"\r\n"，它回 "WIFI CONNECTED" —— 连上了
 *
 * \r\n 是回车+换行，每条 AT 指令必须以这个结尾，就像你在终端按回车。
 *
 * 这个驱动的功能：
 *   1. 初始化 USART2
 *   2. 发送 AT 指令并等待回复
 *   3. 连接 WiFi
 *   4. 建立 TCP 连接，上报事件到服务器
 */

#include "esp8266.h"
#include "delay.h"
#include <string.h>
#include <stdio.h>

/* ========== 硬件配置 ========== */
#define ESP_USART           USART2
#define ESP_USART_RCC       RCC_APB1Periph_USART2
#define ESP_GPIO_RCC        RCC_AHB1Periph_GPIOA
#define ESP_TX_PIN          GPIO_Pin_2
#define ESP_RX_PIN          GPIO_Pin_3
#define ESP_TX_SOURCE       GPIO_PinSource2
#define ESP_RX_SOURCE       GPIO_PinSource3
#define ESP_GPIO_AF         GPIO_AF_USART2

/* ========== 接收缓冲区 ========== */
#define RX_BUF_SIZE         512
static uint8_t espRxBuf[RX_BUF_SIZE];
static volatile uint16_t espRxIndex = 0;

/* ========== USART2 初始化 ========== */

/**
 * USART2_Init - 初始化 USART2（ESP8266 用）
 *
 * 和 USART1 初始化的套路一模一样，区别：
 *   - USART2 挂在 APB1 总线（低速），时钟命令不同
 *   - 引脚是 PA2(TX) 和 PA3(RX)
 *   - 中断通道是 USART2_IRQn
 *
 * 你会发现：所有 USART 初始化都是一个模板，改引脚和时钟就行。
 * 以后需要第 3 个串口？复制粘贴改一改就好。
 */
static void USART2_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    USART_InitTypeDef USART_InitStruct;
    NVIC_InitTypeDef NVIC_InitStruct;

    /* 1. 开时钟
     * 注意：USART2 在 APB1 上（和 USART1 不一样！）
     * APB1 是低速总线（42MHz），APB2 是高速总线（84MHz） */
    RCC_APB1PeriphClockCmd(ESP_USART_RCC, ENABLE);
    RCC_AHB1PeriphClockCmd(ESP_GPIO_RCC, ENABLE);

    /* 2. GPIO 设为复用功能 */
    GPIO_PinAFConfig(GPIOA, ESP_TX_SOURCE, ESP_GPIO_AF);
    GPIO_PinAFConfig(GPIOA, ESP_RX_SOURCE, ESP_GPIO_AF);

    GPIO_InitStruct.GPIO_Pin   = ESP_TX_PIN | ESP_RX_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &GPIO_InitStruct);

    /* 3. USART 参数 */
    USART_InitStruct.USART_BaudRate            = 115200;
    USART_InitStruct.USART_WordLength           = USART_WordLength_8b;
    USART_InitStruct.USART_StopBits             = USART_StopBits_1;
    USART_InitStruct.USART_Parity               = USART_Parity_No;
    USART_InitStruct.USART_HardwareFlowControl  = USART_HardwareFlowControl_None;
    USART_InitStruct.USART_Mode                 = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(ESP_USART, &USART_InitStruct);

    /* 4. 接收中断 */
    USART_ITConfig(ESP_USART, USART_IT_RXNE, ENABLE);

    NVIC_InitStruct.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStruct.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStruct);

    /* 5. 使能 */
    USART_Cmd(ESP_USART, ENABLE);
}

/**
 * USART2_IRQHandler - USART2 中断处理函数
 *
 * 和 USART1 的中断处理一样，每收到 1 字节触发一次。
 * 把数据存到 espRxBuf 里，供后续解析回复用。
 */
void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(ESP_USART, USART_IT_RXNE) != RESET) {
        uint8_t data = USART_ReceiveData(ESP_USART);
        if (espRxIndex < RX_BUF_SIZE - 1) {
            espRxBuf[espRxIndex++] = data;
            espRxBuf[espRxIndex] = '\0';  /* 保持字符串结尾 */
        }
        USART_ClearITPendingBit(ESP_USART, USART_IT_RXNE);
    }
}

/* ========== 发送函数 ========== */

/**
 * ESP_SendString - 发送一个字符串（AT 指令）
 *
 * AT 指令都是文本，用字符串发送最方便。
 * 逐字节发送，和指纹模块的发送函数一样。
 */
static void ESP_SendString(const char *str)
{
    while (*str) {
        while (USART_GetFlagStatus(ESP_USART, USART_FLAG_TXE) == RESET);
        USART_SendData(ESP_USART, *str);
        str++;
    }
    while (USART_GetFlagStatus(ESP_USART, USART_FLAG_TC) == RESET);
}

/**
 * ESP_SendATCommand - 发送 AT 指令并等待回复
 *
 * 这是整个驱动最核心的函数。
 *
 * 流程：
 *   1. 清空接收缓冲区
 *   2. 发送指令（自动加 \r\n）
 *   3. 等待回复（检查缓冲区里有没有期望的字符串）
 *   4. 返回是否成功
 *
 * 参数：
 *   cmd       - AT 指令（不含 \r\n，函数自动加）
 *   expected  - 期望的回复关键字（比如 "OK" 或 "CONNECTED"）
 *   timeout   - 超时时间（毫秒）
 *
 * 返回值：
 *   1 = 收到了期望的回复
 *   0 = 超时或收到 ERROR
 *
 * 举个例子：
 *   ESP_SendATCommand("AT", "OK", 2000)
 *   → 发送 "AT\r\n"
 *   → 等最多 2000ms
 *   → 如果缓冲区里出现 "OK" 就返回 1
 */
uint8_t ESP_SendATCommand(const char *cmd, const char *expected, uint32_t timeout)
{
    uint32_t tick = 0;

    /* 清空接收缓冲区 */
    espRxIndex = 0;
    memset(espRxBuf, 0, RX_BUF_SIZE);

    /* 发送指令 + 回车换行 */
    ESP_SendString(cmd);
    ESP_SendString("\r\n");

    /* 等待回复 */
    while (tick < timeout) {
        Delay_ms(10);
        tick += 10;

        /* 在缓冲区里查找期望的字符串 */
        if (strstr((char *)espRxBuf, expected) != NULL) {
            return 1;  /* 找到了！ */
        }

        /* 如果收到 ERROR，直接返回失败 */
        if (strstr((char *)espRxBuf, "ERROR") != NULL) {
            return 0;
        }
    }

    return 0;  /* 超时 */
}

/* ========== 初始化 ========== */

/**
 * ESP8266_Init - 初始化 ESP8266
 *
 * 初始化流程：
 *   1. 初始化 USART2
 *   2. 测试通信（发 AT）
 *   3. 设为 Station 模式（客户端模式，用来连路由器）
 *   4. 关闭多连接（我们只用一个 TCP 连接）
 *
 * 注意：这里不连 WiFi，因为 WiFi 名和密码需要配置。
 * 连 WiFi 在 ESP8266_ConnectWiFi() 里做。
 */
void ESP8266_Init(void)
{
    USART2_Init();
    Delay_ms(1000);  /* ESP8266 上电启动需要时间（约 1 秒） */

    /* 测试通信 */
    ESP_SendATCommand("AT", "OK", 2000);

    /* 关闭回显（ESP8266 默认会把你发的指令再回显一次，关掉方便解析回复） */
    ESP_SendATCommand("ATE0", "OK", 2000);

    /* 设为 Station 模式
     * CWMODE=1 → Station 模式（连别人，像手机连路由器）
     * CWMODE=2 → AP 模式（被别人连，像路由器）
     * CWMODE=3 → Station + AP 混合模式
     * 我们用 Station 模式，因为它能连到互联网 */
    ESP_SendATCommand("AT+CWMODE=1", "OK", 2000);

    /* 关闭多连接
     * CIPMUX=0 → 单连接模式（我们只连一个服务器）
     * CIPMUX=1 → 多连接模式（可以同时连多个服务器） */
    ESP_SendATCommand("AT+CIPMUX=0", "OK", 2000);
}

/* ========== 连接函数 ========== */

/**
 * ESP8266_ConnectWiFi - 连接 WiFi 热点
 *
 * 参数：
 *   ssid     - WiFi 名称
 *   password - WiFi 密码
 *
 * 返回值：
 *   1 = 连接成功
 *   0 = 连接失败
 *
 * 注意：这个指令可能需要 10~15 秒才能完成连接，超时要设够。
 */
uint8_t ESP8266_ConnectWiFi(const char *ssid, const char *password)
{
    char cmd[128];

    /* 拼接指令：AT+CWJAP="WiFi名","密码" */
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", ssid, password);

    /* 发送并等待（最多等 15 秒） */
    return ESP_SendATCommand(cmd, "OK", 15000);
}

/**
 * ESP8266_ConnectTCP - 建立 TCP 连接到服务器
 *
 * 参数：
 *   ip   - 服务器 IP 或域名
 *   port - 服务器端口
 *
 * 返回值：
 *   1 = 连接成功
 *   0 = 连接失败
 */
static uint8_t ESP8266_ConnectTCP(const char *ip, uint16_t port)
{
    char cmd[128];

    /* 拼接指令：AT+CIPSTART="TCP","IP",端口 */
    sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%d", ip, port);

    return ESP_SendATCommand(cmd, "OK", 5000);
}

/* ========== 事件上报 ========== */

/**
 * ESP8266_SendEvent - 向服务器上报事件
 *
 * 这是整个智能门锁最"智能"的部分 ——
 * 每次开锁、失败、锁定都会通知服务器。
 *
 * 流程：
 *   1. 建立 TCP 连接
 *   2. 发送数据长度（AT+CIPSEND=n）
 *   3. 发送实际数据（JSON 格式）
 *
 * 参数：
 *   event - 事件描述字符串，比如 "UNLOCK: fingerprint"
 *
 * JSON 格式：
 *   {"device":"smart-lock","event":"UNLOCK: fingerprint","time":12345}
 *
 * 为什么用 JSON？
 *   服务器端最容易解析。Python 的 json.loads()、JavaScript 的 JSON.parse() 都能直接处理。
 */
uint8_t ESP8266_SendEvent(const char *event)
{
    char json[256];
    char cmd[32];
    uint16_t len;

    /* 1. 连接服务器
     * 这里用了一个示例 IP 和端口，实际使用时改成你的服务器地址
     * 可以是局域网 IP（192.168.1.100）或公网域名 */
    if (!ESP8266_ConnectTCP("192.168.1.100", 8080)) {
        return 0;
    }

    /* 2. 构造 JSON 数据 */
    sprintf(json, "{\"device\":\"smart-lock\",\"event\":\"%s\"}", event);
    len = strlen(json);

    /* 3. 告诉 ESP8266 要发多少字节
     * AT+CIPSEND=字节数
     * 发完这条后，ESP8266 会回一个 ">" 提示符
     * 然后你再发实际的数据 */
    sprintf(cmd, "AT+CIPSEND=%d", len);
    if (!ESP_SendATCommand(cmd, ">", 3000)) {
        return 0;
    }

    /* 4. 发送实际数据 */
    ESP_SendString(json);
    Delay_ms(100);

    /* 5. 关闭连接（省资源） */
    ESP_SendATCommand("AT+CIPCLOSE", "OK", 2000);

    return 1;
}

/**
 * ESP8266_GetIP - 获取 ESP8266 的 IP 地址
 *
 * 连接 WiFi 后，可以用这个函数查看分配到的 IP。
 * 调试时很有用：确认模块确实连上了网络。
 *
 * IP 地址会存在 espRxBuf 里，格式：
 *   +CIFSR:STAIP,"192.168.1.105"
 */
uint8_t ESP8266_GetIP(char *ipBuf, uint8_t bufSize)
{
    espRxIndex = 0;
    memset(espRxBuf, 0, RX_BUF_SIZE);

    ESP_SendString("AT+CIFSR\r\n");
    Delay_ms(2000);

    /* 从回复中提取 IP 地址
     * 回复格式：+CIFSR:STAIP,"192.168.1.105"
     * 找到 STAIP 后面的引号，提取 IP */
    char *ipStart = strstr((char *)espRxBuf, "STAIP,\"");
    if (ipStart != NULL) {
        ipStart += 7;  /* 跳过 STAIP," */
        char *ipEnd = strchr(ipStart, '\"');
        if (ipEnd != NULL) {
            uint8_t len = ipEnd - ipStart;
            if (len < bufSize) {
                memcpy(ipBuf, ipStart, len);
                ipBuf[len] = '\0';
                return 1;
            }
        }
    }

    return 0;
}
