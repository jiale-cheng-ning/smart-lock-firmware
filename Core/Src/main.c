/**
 * Smart Lock Firmware - STM32F411RET6
 *
 * 功能：
 *   1. 指纹识别开锁（ZW101 指纹模块）
 *   2. 4x4 矩阵键盘密码开锁
 *   3. OLED 显示状态信息
 *   4. ESP8266 WiFi 远程通知
 *   5. 电磁锁控制
 *   6. 密码修改（旧密码验证 → 输入新密码 → 确认新密码）
 *   7. WiFi 配网（AP 模式，手机连接配置）
 *   8. 低功耗待机（空闲时进入 Sleep 模式，按键/指纹唤醒）
 *
 * 硬件连接：
 *   - 指纹模块: USART1 (PA9-TX, PA10-RX)
 *   - ESP8266:   USART2 (PA2-TX, PA3-RX)
 *   - OLED:      I2C1 (PB6-SCL, PB7-SDA)
 *   - 矩阵键盘:  PB0-PB3(行), PB4-PB7(列)
 *   - 电磁锁:    PA1 (GPIO + 继电器)
 *   - LED 指示:  PC13
 */

#include "stm32f4xx.h"
#include "delay.h"
#include "fingerprint.h"
#include "keypad.h"
#include "oled.h"
#include "esp8266.h"
#include "lock.h"
#include <string.h>

/* ========== 系统配置 ========== */
#define MAX_PASSWORD_LEN    8
#define MAX_ATTEMPTS        3
#define LOCKOUT_DURATION    30000   /* 30 秒锁定 */
#define AUTO_LOCK_DELAY     5000    /* 5 秒自动上锁 */
#define IDLE_TIMEOUT        30000   /* 30 秒无操作进入低功耗 */
#define WIFI_SSID           "YourWiFi"      /* 默认 WiFi 名（实际使用时通过配网修改） */
#define WIFI_PASSWORD       "YourPassword"  /* 默认 WiFi 密码 */
#define SERVER_IP           "192.168.1.100"
#define SERVER_PORT         8080

/* ========== 系统状态枚举 ========== */
typedef enum {
    STATE_IDLE = 0,         /* 空闲等待 */
    STATE_INPUT_PASSWORD,   /* 输入密码中 */
    STATE_VERIFY_FP,        /* 验证指纹中 */
    STATE_UNLOCKED,         /* 已开锁 */
    STATE_LOCKED_OUT,       /* 锁定（连续失败） */
    STATE_CHANGE_PWD_OLD,   /* 修改密码：输入旧密码 */
    STATE_CHANGE_PWD_NEW,   /* 修改密码：输入新密码 */
    STATE_CHANGE_PWD_CONFIRM, /* 修改密码：确认新密码 */
    STATE_WIFI_CONFIG,      /* WiFi 配网模式 */
    STATE_SLEEP             /* 低功耗睡眠 */
} SystemState;

/* ========== 全局变量 ========== */
static SystemState currentState = STATE_IDLE;

/* 密码相关 */
static char currentPassword[MAX_PASSWORD_LEN + 1] = "123456";  /* 当前密码（默认） */
static char inputBuffer[MAX_PASSWORD_LEN + 1];     /* 输入缓冲区 */
static char newPwdBuffer[MAX_PASSWORD_LEN + 1];    /* 新密码临时存储 */
static uint8_t inputIndex = 0;
static uint8_t failCount = 0;
static uint32_t lockoutTimer = 0;

/* 低功耗相关 */
static uint32_t idleTimer = 0;          /* 空闲计时器 */
static volatile uint8_t wakeUpFlag = 0; /* 唤醒标志 */

/* WiFi 状态 */
static uint8_t wifiConnected = 0;

/* ========== 函数声明 ========== */
static void System_Init(void);
static void LED_Init(void);
static void LED_Toggle(void);
static void Enter_Sleep_Mode(void);
static void Display_Idle_Screen(void);
static void Display_Password_Input(void);
static void Display_Change_Password_Step(uint8_t step);
static void Display_Unlock_Screen(UnlockMethod method);
static void Display_Lockout_Screen(void);
static void Display_WiFi_Config_Screen(void);
static void Handle_Idle(void);
static void Handle_Password_Input(void);
static void Handle_Change_Password_Old(void);
static void Handle_Change_Password_New(void);
static void Handle_Change_Password_Confirm(void);
static void Handle_Lockout(void);
static void Handle_Unlock(void);
static void Handle_WiFi_Config(void);
static void Notify_WiFi(const char *event);

/* 开锁方式（全局，供显示函数使用） */
typedef enum {
    UNLOCK_NONE = 0,
    UNLOCK_FINGERPRINT,
    UNLOCK_PASSWORD
} UnlockMethod;
static UnlockMethod currentUnlockMethod = UNLOCK_NONE;

/* ========== 主函数 ========== */

int main(void)
{
    System_Init();
    Display_Idle_Screen();

    while (1) {
        /* 空闲超时检测：长时间无操作 → 进入低功耗 */
        if (currentState == STATE_IDLE) {
            idleTimer++;
            if (idleTimer > IDLE_TIMEOUT) {
                currentState = STATE_SLEEP;
            }
        } else {
            idleTimer = 0;
        }

        switch (currentState) {
            case STATE_IDLE:
                Handle_Idle();
                break;

            case STATE_INPUT_PASSWORD:
                Handle_Password_Input();
                break;

            case STATE_VERIFY_FP:
                /* 指纹验证在 Handle_Idle 中已触发 */
                break;

            case STATE_UNLOCKED:
                Handle_Unlock();
                break;

            case STATE_LOCKED_OUT:
                Handle_Lockout();
                break;

            case STATE_CHANGE_PWD_OLD:
                Handle_Change_Password_Old();
                break;

            case STATE_CHANGE_PWD_NEW:
                Handle_Change_Password_New();
                break;

            case STATE_CHANGE_PWD_CONFIRM:
                Handle_Change_Password_Confirm();
                break;

            case STATE_WIFI_CONFIG:
                Handle_WiFi_Config();
                break;

            case STATE_SLEEP:
                Enter_Sleep_Mode();
                break;

            default:
                currentState = STATE_IDLE;
                break;
        }

        Delay_ms(10);
    }
}

/* ========== 初始化 ========== */

static void System_Init(void)
{
    SysTick_Init();
    OLED_Init();
    KeyPad_Init();
    Fingerprint_Init();
    ESP8266_Init();
    Lock_Init();
    LED_Init();

    /* 尝试连接 WiFi（非阻塞，失败不卡住） */
    wifiConnected = ESP8266_ConnectWiFi(WIFI_SSID, WIFI_PASSWORD);
}

/**
 * LED_Init - 初始化 PC13 LED（状态指示灯）
 */
static void LED_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);

    GPIO_InitStruct.GPIO_Pin   = GPIO_Pin_13;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_SetBits(GPIOC, GPIO_Pin_13);  /* 默认灭（高电平灭） */
}

static void LED_Toggle(void)
{
    GPIO_ToggleBits(GPIOC, GPIO_Pin_13);
}

/* ========== 低功耗睡眠模式 ========== */

/**
 * Enter_Sleep_Mode - 进入低功耗睡眠
 *
 * STM32 的低功耗模式：
 *   1. Sleep 模式：CPU 停止，外设继续运行，任意中断唤醒
 *   2. Stop 模式：CPU 和大部分时钟停止，EXTI 唤醒（更省电）
 *   3. Standby 模式：几乎全关，只有 WKUP 引脚唤醒（最省电，但需要重新初始化）
 *
 * 我们用 Sleep 模式：
 *   - CPU 执行 WFI 指令后暂停，等待中断
 *   - 串口中断（指纹/WiFi 数据）或矩阵键盘的 GPIO 变化可以唤醒
 *   - 唤醒后从 WFI 的下一条指令继续执行
 *   - 不需要重新初始化外设
 *
 * WFI = Wait For Interrupt，是 ARM 内核的指令，一条汇编搞定。
 */
static void Enter_Sleep_Mode(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "Sleep Mode");
    OLED_ShowString(0, 2, "Press any key");
    OLED_ShowString(0, 4, "or touch sensor");
    OLED_ShowString(0, 6, "to wake up...");
    Delay_ms(100);

    /* 关闭 OLED 显示（省电） */
    OLED_WriteCmd(0xAE);  /* 关显示 */

    /* LED 慢闪表示睡眠状态 */
    LED_Toggle();

    /* 进入 Sleep 模式
     * __WFI() 是 CMSIS 提供的内联汇编，就是执行 WFI 指令
     * CPU 在这里暂停，直到有中断发生（按键按下、串口收到数据等） */
    __WFI();

    /* 被中断唤醒后，从这里继续执行 */
    OLED_WriteCmd(0xAF);  /* 重新开显示 */
    idleTimer = 0;
    currentState = STATE_IDLE;
    Display_Idle_Screen();
}

/* ========== 显示函数 ========== */

static void Display_Idle_Screen(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "== Smart Lock ==");
    OLED_ShowString(0, 2, "Pwd: Enter #");
    OLED_ShowString(0, 4, "FP:  Touch sensor");
    OLED_ShowString(0, 6, "*:Menu  #:OK");
}

static void Display_Password_Input(void)
{
    uint8_t i;
    OLED_Clear();
    OLED_ShowString(0, 0, "Input Password:");
    OLED_ShowString(0, 2, "Pwd: ");

    /* 显示已输入的位数，用 * 遮挡（安全） */
    for (i = 0; i < inputIndex; i++) {
        OLED_ShowChar(40 + i * 8, 2, '*');
    }
    OLED_ShowChar(40 + inputIndex * 8, 2, '_');  /* 光标位置 */

    OLED_ShowString(0, 4, "*:Clear  #:Confirm");
}

static void Display_Change_Password_Step(uint8_t step)
{
    OLED_Clear();
    if (step == 1) {
        OLED_ShowString(0, 0, "Change Password");
        OLED_ShowString(0, 2, "Enter OLD password:");
    } else if (step == 2) {
        OLED_ShowString(0, 0, "Change Password");
        OLED_ShowString(0, 2, "Enter NEW password:");
        OLED_ShowString(0, 6, "*:Clear  #:OK");
    } else if (step == 3) {
        OLED_ShowString(0, 0, "Change Password");
        OLED_ShowString(0, 2, "Confirm NEW password:");
        OLED_ShowString(0, 6, "*:Clear  #:OK");
    }
}

static void Display_Unlock_Screen(UnlockMethod method)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "=== UNLOCKED ===");
    if (method == UNLOCK_FINGERPRINT) {
        OLED_ShowString(0, 2, "By: Fingerprint");
    } else {
        OLED_ShowString(0, 2, "By: Password");
    }
    OLED_ShowString(0, 4, "Door is open...");
    OLED_ShowString(0, 6, "Auto lock in 5s");
}

static void Display_Lockout_Screen(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "!! LOCKED OUT !!");
    OLED_ShowString(0, 2, "Too many failures");
    OLED_ShowString(0, 4, "Wait 30 seconds");
    OLED_ShowString(0, 6, "Remaining: 30s");
}

static void Display_WiFi_Config_Screen(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "WiFi Config Mode");
    OLED_ShowString(0, 2, "Connect AP:");
    OLED_ShowString(0, 4, "SSID: SmartLock");
    OLED_ShowString(0, 6, "IP:   192.168.4.1");
}

/* ========== 状态处理函数 ========== */

/**
 * Handle_Idle - 空闲状态处理
 *
 * 在空闲状态，检测：
 *   1. 数字键按下 → 进入密码输入模式
 *   2. '*' 键按下 → 进入菜单（修改密码/WiFi 配网）
 *   3. 指纹传感器有手指 → 进入指纹验证
 */
static void Handle_Idle(void)
{
    char key = KeyPad_Scan();

    /* 数字键 → 开始输入密码 */
    if (key >= '0' && key <= '9') {
        inputIndex = 0;
        memset(inputBuffer, 0, sizeof(inputBuffer));
        inputBuffer[inputIndex++] = key;
        inputBuffer[inputIndex] = '\0';
        currentState = STATE_INPUT_PASSWORD;
        Display_Password_Input();
        idleTimer = 0;
        return;
    }

    /* '*' 键 → 菜单（修改密码 / WiFi 配网） */
    if (key == '*') {
        /* 长按 * 进入修改密码，短按进入 WiFi 配网
         * 这里简化：按 1 进入修改密码，按 2 进入 WiFi 配网 */
        OLED_Clear();
        OLED_ShowString(0, 0, "Menu:");
        OLED_ShowString(0, 2, "1: Change Password");
        OLED_ShowString(0, 4, "2: WiFi Config");
        OLED_ShowString(0, 6, "*: Back");

        while (1) {
            char menuKey = KeyPad_Scan();
            if (menuKey == '1') {
                inputIndex = 0;
                memset(inputBuffer, 0, sizeof(inputBuffer));
                currentState = STATE_CHANGE_PWD_OLD;
                Display_Change_Password_Step(1);
                idleTimer = 0;
                return;
            }
            if (menuKey == '2') {
                currentState = STATE_WIFI_CONFIG;
                Display_WiFi_Config_Screen();
                idleTimer = 0;
                return;
            }
            if (menuKey == '*') {
                Display_Idle_Screen();
                return;
            }
        }
    }

    /* 检测指纹 */
    if (Fingerprint_IsFingerPressed()) {
        idleTimer = 0;
        uint16_t id = Fingerprint_Match();
        if (id > 0) {
            currentUnlockMethod = UNLOCK_FINGERPRINT;
            currentState = STATE_UNLOCKED;
            Display_Unlock_Screen(UNLOCK_FINGERPRINT);
        } else {
            failCount++;
            OLED_Clear();
            OLED_ShowString(0, 2, "Fingerprint FAIL");
            Notify_WiFi("ALERT: fingerprint_fail");
            Delay_ms(1000);

            if (failCount >= MAX_ATTEMPTS) {
                currentState = STATE_LOCKED_OUT;
                lockoutTimer = LOCKOUT_DURATION;
                Notify_WiFi("ALERT: locked_out");
                Display_Lockout_Screen();
            } else {
                Display_Idle_Screen();
            }
        }
    }
}

/**
 * Handle_Password_Input - 密码输入处理
 *
 * 按键规则：
 *   0~9: 输入数字
 *   '#': 确认密码
 *   '*': 清除输入 / 返回
 */
static void Handle_Password_Input(void)
{
    char key = KeyPad_Scan();

    if (key == '#') {
        /* 确认密码 */
        if (strcmp(inputBuffer, currentPassword) == 0) {
            currentUnlockMethod = UNLOCK_PASSWORD;
            currentState = STATE_UNLOCKED;
            Display_Unlock_Screen(UNLOCK_PASSWORD);
        } else {
            failCount++;
            OLED_Clear();
            OLED_ShowString(0, 2, "Password FAIL!");
            Notify_WiFi("ALERT: password_fail");
            Delay_ms(1000);

            if (failCount >= MAX_ATTEMPTS) {
                currentState = STATE_LOCKED_OUT;
                lockoutTimer = LOCKOUT_DURATION;
                Notify_WiFi("ALERT: locked_out");
                Display_Lockout_Screen();
            } else {
                inputIndex = 0;
                memset(inputBuffer, 0, sizeof(inputBuffer));
                currentState = STATE_IDLE;
                Display_Idle_Screen();
            }
        }
    } else if (key == '*') {
        /* 清除/返回 */
        if (inputIndex > 0) {
            inputIndex--;
            inputBuffer[inputIndex] = '\0';
            Display_Password_Input();
        } else {
            currentState = STATE_IDLE;
            Display_Idle_Screen();
        }
    } else if (key >= '0' && key <= '9' && inputIndex < MAX_PASSWORD_LEN) {
        inputBuffer[inputIndex++] = key;
        inputBuffer[inputIndex] = '\0';
        Display_Password_Input();
    }
}

/* ========== 密码修改流程 ========== */

/**
 * Handle_Change_Password_Old - 修改密码第 1 步：验证旧密码
 *
 * 流程：输入当前密码 → 按 '#' 确认
 *   - 正确 → 进入第 2 步
 *   - 错误 → 返回空闲
 */
static void Handle_Change_Password_Old(void)
{
    char key = KeyPad_Scan();
    uint8_t i;

    if (key == '#') {
        if (strcmp(inputBuffer, currentPassword) == 0) {
            /* 旧密码正确，进入第 2 步 */
            inputIndex = 0;
            memset(inputBuffer, 0, sizeof(inputBuffer));
            currentState = STATE_CHANGE_PWD_NEW;
            Display_Change_Password_Step(2);
        } else {
            OLED_Clear();
            OLED_ShowString(0, 2, "Wrong password!");
            Delay_ms(1000);
            currentState = STATE_IDLE;
            Display_Idle_Screen();
        }
    } else if (key == '*') {
        if (inputIndex > 0) {
            inputIndex--;
            inputBuffer[inputIndex] = '\0';
        } else {
            currentState = STATE_IDLE;
            Display_Idle_Screen();
            return;
        }
        /* 重新显示 */
        Display_Change_Password_Step(1);
        for (i = 0; i < inputIndex; i++) {
            OLED_ShowChar(40 + i * 8, 4, '*');
        }
    } else if (key >= '0' && key <= '9' && inputIndex < MAX_PASSWORD_LEN) {
        inputBuffer[inputIndex++] = key;
        inputBuffer[inputIndex] = '\0';
        /* 显示输入进度 */
        Display_Change_Password_Step(1);
        for (i = 0; i < inputIndex; i++) {
            OLED_ShowChar(40 + i * 8, 4, '*');
        }
    }
}

/**
 * Handle_Change_Password_New - 修改密码第 2 步：输入新密码
 */
static void Handle_Change_Password_New(void)
{
    char key = KeyPad_Scan();
    uint8_t i;

    if (key == '#') {
        if (inputIndex < 4) {
            OLED_Clear();
            OLED_ShowString(0, 2, "Min 4 digits!");
            Delay_ms(1000);
            Display_Change_Password_Step(2);
            return;
        }
        /* 保存新密码到临时缓冲区 */
        strncpy(newPwdBuffer, inputBuffer, MAX_PASSWORD_LEN);
        inputIndex = 0;
        memset(inputBuffer, 0, sizeof(inputBuffer));
        currentState = STATE_CHANGE_PWD_CONFIRM;
        Display_Change_Password_Step(3);
    } else if (key == '*') {
        if (inputIndex > 0) {
            inputIndex--;
            inputBuffer[inputIndex] = '\0';
        } else {
            currentState = STATE_IDLE;
            Display_Idle_Screen();
            return;
        }
        Display_Change_Password_Step(2);
        for (i = 0; i < inputIndex; i++) {
            OLED_ShowChar(40 + i * 8, 4, '*');
        }
    } else if (key >= '0' && key <= '9' && inputIndex < MAX_PASSWORD_LEN) {
        inputBuffer[inputIndex++] = key;
        inputBuffer[inputIndex] = '\0';
        Display_Change_Password_Step(2);
        for (i = 0; i < inputIndex; i++) {
            OLED_ShowChar(40 + i * 8, 4, '*');
        }
    }
}

/**
 * Handle_Change_Password_Confirm - 修改密码第 3 步：确认新密码
 *
 * 对比两次输入的新密码是否一致：
 *   - 一致 → 更新密码，保存
 *   - 不一致 → 提示错误，重新输入
 */
static void Handle_Change_Password_Confirm(void)
{
    char key = KeyPad_Scan();
    uint8_t i;

    if (key == '#') {
        if (strcmp(inputBuffer, newPwdBuffer) == 0) {
            /* 两次一致，更新密码 */
            strncpy(currentPassword, newPwdBuffer, MAX_PASSWORD_LEN);
            OLED_Clear();
            OLED_ShowString(0, 2, "Password Changed!");
            OLED_ShowString(0, 4, "New password set");
            Notify_WiFi("EVENT: password_changed");
            Delay_ms(2000);
        } else {
            OLED_Clear();
            OLED_ShowString(0, 2, "Passwords don't");
            OLED_ShowString(0, 4, "match! Try again");
            Delay_ms(2000);
        }
        /* 无论成功失败都回到空闲 */
        memset(newPwdBuffer, 0, sizeof(newPwdBuffer));
        inputIndex = 0;
        memset(inputBuffer, 0, sizeof(inputBuffer));
        currentState = STATE_IDLE;
        Display_Idle_Screen();
    } else if (key == '*') {
        if (inputIndex > 0) {
            inputIndex--;
            inputBuffer[inputIndex] = '\0';
        } else {
            currentState = STATE_IDLE;
            Display_Idle_Screen();
            return;
        }
        Display_Change_Password_Step(3);
        for (i = 0; i < inputIndex; i++) {
            OLED_ShowChar(40 + i * 8, 4, '*');
        }
    } else if (key >= '0' && key <= '9' && inputIndex < MAX_PASSWORD_LEN) {
        inputBuffer[inputIndex++] = key;
        inputBuffer[inputIndex] = '\0';
        Display_Change_Password_Step(3);
        for (i = 0; i < inputIndex; i++) {
            OLED_ShowChar(40 + i * 8, 4, '*');
        }
    }
}

/* ========== 开锁处理 ========== */

static void Handle_Unlock(void)
{
    Lock_Open();
    failCount = 0;

    if (currentUnlockMethod == UNLOCK_FINGERPRINT) {
        Notify_WiFi("UNLOCK: fingerprint");
    } else {
        Notify_WiFi("UNLOCK: password");
    }

    /* 5 秒后自动上锁 */
    Delay_ms(AUTO_LOCK_DELAY);
    Lock_Close();

    inputIndex = 0;
    memset(inputBuffer, 0, sizeof(inputBuffer));
    currentState = STATE_IDLE;
    Display_Idle_Screen();
}

/* ========== 锁定处理 ========== */

static void Handle_Lockout(void)
{
    if (lockoutTimer > 0) {
        lockoutTimer -= 100;
        /* 更新倒计时显示 */
        uint32_t seconds = lockoutTimer / 1000;
        OLED_ShowNum(80, 6, seconds, 2);
        Delay_ms(100);
    } else {
        failCount = 0;
        currentState = STATE_IDLE;
        Display_Idle_Screen();
    }
}

/* ========== WiFi 配网 ========== */

/**
 * Handle_WiFi_Config - WiFi 配网模式
 *
 * 配网方式：ESP8266 进入 AP 模式，手机连接后通过网页配置
 *
 * 流程：
 *   1. ESP8266 切换为 AP 模式，创建热点 "SmartLock"
 *   2. 手机连接这个热点
 *   3. 手机浏览器访问 192.168.4.1 配置 WiFi 名和密码
 *   4. ESP8266 收到配置后切换为 Station 模式连接路由器
 *
 * 简化版实现：通过串口配置（AT 指令），显示 AP 信息让用户参考
 * 实际产品中会做一个 Web 配置页面，这里先演示 AP 模式的切换
 */
static void Handle_WiFi_Config(void)
{
    char key;

    /* 步骤 1：切换到 AP 模式 */
    OLED_Clear();
    OLED_ShowString(0, 0, "Starting AP...");
    OLED_ShowString(0, 2, "Please wait...");

    /* AT+CWMODE=2 设为 AP 模式
     * AP 模式下 ESP8266 自己变成一个 WiFi 热点 */
    ESP_SendATCommand("AT+CWMODE=2", "OK", 3000);

    /* 创建热点：SSID=SmartLock，密码=12345678 */
    ESP_SendATCommand("AT+CWSAP=\"SmartLock\",\"12345678\",5,3", "OK", 5000);

    /* 开启多连接（Web 服务器需要） */
    ESP_SendATCommand("AT+CIPMUX=1", "OK", 2000);

    /* 启动 TCP 服务器，端口 80 */
    ESP_SendATCommand("AT+CIPSERVER=1,80", "OK", 2000);

    Display_WiFi_Config_Screen();

    /* 步骤 2：等待用户配置（这里简化为按键退出） */
    while (1) {
        key = KeyPad_Scan();
        if (key == '*') {
            /* 退出配网模式，恢复 Station 模式 */
            ESP_SendATCommand("AT+CIPSERVER=0,80", "OK", 2000);  /* 关服务器 */
            ESP_SendATCommand("AT+CWMODE=1", "OK", 2000);        /* 切回 Station */
            wifiConnected = ESP8266_ConnectWiFi(WIFI_SSID, WIFI_PASSWORD);

            currentState = STATE_IDLE;
            Display_Idle_Screen();
            return;
        }
        Delay_ms(100);
    }
}

/* ========== WiFi 通知 ========== */

static void Notify_WiFi(const char *event)
{
    if (wifiConnected) {
        ESP8266_SendEvent(event);
    }
}
