/**
 * Smart Lock Firmware - STM32F411RET6
 *
 * 功能：
 *   1. 指纹识别开锁（ZW101 指纹模块）
 *   2. 4x4 矩阵键盘密码开锁
 *   3. OLED 显示状态信息
 *   4. ESP8266 WiFi 远程通知
 *   5. 电磁锁控制
 *
 * 硬件连接：
 *   - 指纹模块: USART2 (PA2-TX, PA3-RX)
 *   - ESP8266:   USART1 (PA9-TX, PA10-RX)
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

/* 开锁方式枚举 */
typedef enum {
    UNLOCK_NONE = 0,
    UNLOCK_FINGERPRINT,
    UNLOCK_PASSWORD
} UnlockMethod;

/* 系统状态 */
typedef enum {
    STATE_IDLE,
    STATE_INPUT_PASSWORD,
    STATE_VERIFY_FINGERPRINT,
    STATE_UNLOCKED,
    STATE_LOCKED_OUT
} SystemState;

static SystemState currentState = STATE_IDLE;
static uint32_t lockoutTimer = 0;

#define MAX_PASSWORD_LEN    8
#define MAX_ATTEMPTS        3
#define LOCKOUT_DURATION    30000  /* 30秒锁定 */

static char password[MAX_PASSWORD_LEN + 1] = "123456";  /* 默认密码 */
static char inputBuffer[MAX_PASSWORD_LEN + 1];
static uint8_t inputIndex = 0;
static uint8_t failCount = 0;

static void System_Init(void);
static void Handle_Idle(void);
static void Handle_Password_Input(void);
static void Handle_Unlock(UnlockMethod method);
static void Handle_Lockout(void);
static void Update_OLED_Display(void);
static void Notify_WiFi(const char *event);

int main(void)
{
    System_Init();

    OLED_ShowString(0, 0, "Smart Lock Ready");
    OLED_ShowString(0, 2, "Pwd: ****");
    OLED_ShowString(0, 4, "FP:  Press finger");

    while (1) {
        switch (currentState) {
            case STATE_IDLE:
                Handle_Idle();
                break;
            case STATE_INPUT_PASSWORD:
                Handle_Password_Input();
                break;
            case STATE_UNLOCKED:
                /* 开锁后5秒自动上锁 */
                break;
            case STATE_LOCKED_OUT:
                Handle_Lockout();
                break;
            default:
                currentState = STATE_IDLE;
                break;
        }
        Update_OLED_Display();
    }
}

static void System_Init(void)
{
    SysTick_Init();
    OLED_Init();
    KeyPad_Init();
    Fingerprint_Init();
    ESP8266_Init();
    Lock_Init();
}

static void Handle_Idle(void)
{
    /* 检测矩阵键盘输入 */
    char key = KeyPad_Scan();
    if (key >= '0' && key <= '9') {
        currentState = STATE_INPUT_PASSWORD;
        inputBuffer[inputIndex++] = key;
        inputBuffer[inputIndex] = '\0';
    }

    /* 检测指纹 */
    if (Fingerprint_IsFingerPressed()) {
        currentState = STATE_VERIFY_FINGERPRINT;
        uint16_t id = Fingerprint_Match();
        if (id != 0) {
            Handle_Unlock(UNLOCK_FINGERPRINT);
        } else {
            failCount++;
            if (failCount >= MAX_ATTEMPTS) {
                currentState = STATE_LOCKED_OUT;
                lockoutTimer = LOCKOUT_DURATION;
                Notify_WiFi("ALERT: fingerprint_fail_locked");
            }
            currentState = STATE_IDLE;
        }
    }
}

static void Handle_Password_Input(void)
{
    char key = KeyPad_Scan();
    if (key == '#') {
        /* 确认密码 */
        if (strcmp(inputBuffer, password) == 0) {
            Handle_Unlock(UNLOCK_PASSWORD);
        } else {
            failCount++;
            inputIndex = 0;
            memset(inputBuffer, 0, sizeof(inputBuffer));
            if (failCount >= MAX_ATTEMPTS) {
                currentState = STATE_LOCKED_OUT;
                lockoutTimer = LOCKOUT_DURATION;
                Notify_WiFi("ALERT: password_fail_locked");
            }
        }
    } else if (key == '*') {
        /* 清除输入 */
        inputIndex = 0;
        memset(inputBuffer, 0, sizeof(inputBuffer));
        currentState = STATE_IDLE;
    } else if (key >= '0' && key <= '9' && inputIndex < MAX_PASSWORD_LEN) {
        inputBuffer[inputIndex++] = key;
        inputBuffer[inputIndex] = '\0';
    }
}

static void Handle_Unlock(UnlockMethod method)
{
    Lock_Open();
    failCount = 0;
    currentState = STATE_UNLOCKED;

    if (method == UNLOCK_FINGERPRINT) {
        OLED_ShowString(0, 6, "UNLOCK: Fingerprint");
        Notify_WiFi("UNLOCK: fingerprint");
    } else {
        OLED_ShowString(0, 6, "UNLOCK: Password");
        Notify_WiFi("UNLOCK: password");
    }

    /* 5秒后自动上锁 */
    Delay_ms(5000);
    Lock_Close();
    currentState = STATE_IDLE;
    inputIndex = 0;
    memset(inputBuffer, 0, sizeof(inputBuffer));
}

static void Handle_Lockout(void)
{
    if (lockoutTimer > 0) {
        lockoutTimer -= 100;
        Delay_ms(100);
    } else {
        failCount = 0;
        currentState = STATE_IDLE;
    }
}

static void Update_OLED_Display(void)
{
    /* 根据状态更新显示内容 */
}

static void Notify_WiFi(const char *event)
{
    ESP8266_SendEvent(event);
}
