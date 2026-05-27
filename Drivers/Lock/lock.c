/**
 * 电磁锁驱动
 *
 * 硬件原理：
 *   STM32 GPIO 输出高/低电平 → 控制继电器模块 → 继电器吸合/断开 → 电磁锁通电/断电
 *
 *   继电器模块（低电平触发）：
 *     PA1 = LOW  → 继电器吸合 → 电磁锁通电 → 门锁打开
 *     PA1 = HIGH → 继电器断开 → 电磁锁断电 → 门锁关闭（默认状态）
 *
 * 为什么用继电器？
 *   STM32 的 GPIO 只能输出 3.3V、几毫安的电流。
 *   电磁锁需要 12V、几百毫安才能工作。
 *   继电器就是"用小电流控制大电流"的开关。
 *   原理和你家里的墙壁开关一样，只不过用电信号代替了手指。
 *
 * 接线：
 *   PA1 ──→ 继电器模块 IN 引脚
 *   继电器 COM ──→ 电磁锁正极
 *   继电器 NO  ──→ 12V 电源正极
 *   电磁锁负极 ──→ 12V 电源负极
 *
 *   COM = Common（公共端），NO = Normally Open（常开端）
 *   继电器吸合时 COM 和 NO 接通 → 电磁锁通电
 */

#include "lock.h"
#include "delay.h"

/* ========== 引脚定义 ========== */
#define LOCK_GPIO_PORT      GPIOA
#define LOCK_GPIO_PIN       GPIO_Pin_1
#define LOCK_GPIO_RCC       RCC_AHB1Periph_GPIOA

/* 锁的当前状态（用于查询） */
static uint8_t lockIsOpen = 0;

/* ========== 初始化 ========== */

/**
 * Lock_Init - 初始化电磁锁的 GPIO
 *
 * 做的事：
 *   1. 开 GPIOA 时钟
 *   2. PA1 配置为推挽输出
 *   3. 默认输出高电平（锁关闭）
 *
 * 和键盘驱动的行引脚配置一模一样，套路都是：
 *   开时钟 → 配模式 → 设初始值
 */
void Lock_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    /* 开时钟 */
    RCC_AHB1PeriphClockCmd(LOCK_GPIO_RCC, ENABLE);

    /* 配置 PA1 为推挽输出 */
    GPIO_InitStruct.GPIO_Pin   = LOCK_GPIO_PIN;
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_OUT;
    GPIO_InitStruct.GPIO_OType = GPIO_OType_PP;    /* 推挽 */
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStruct.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_Init(LOCK_GPIO_PORT, &GPIO_InitStruct);

    /* 默认高电平 → 继电器断开 → 门锁关闭（安全状态）
     * "安全状态"的意思是：如果程序崩溃或没初始化完，门是锁着的 */
    GPIO_SetBits(LOCK_GPIO_PORT, LOCK_GPIO_PIN);
    lockIsOpen = 0;
}

/* ========== 控制函数 ========== */

/**
 * Lock_Open - 开锁
 *
 * 输出低电平 → 继电器吸合 → 电磁锁通电 → 门打开
 */
void Lock_Open(void)
{
    GPIO_ResetBits(LOCK_GPIO_PORT, LOCK_GPIO_PIN);  /* PA1 = LOW */
    lockIsOpen = 1;
}

/**
 * Lock_Close - 上锁
 *
 * 输出高电平 → 继电器断开 → 电磁锁断电 → 门锁上
 */
void Lock_Close(void)
{
    GPIO_SetBits(LOCK_GPIO_PORT, LOCK_GPIO_PIN);    /* PA1 = HIGH */
    lockIsOpen = 0;
}

/**
 * Lock_IsOpen - 查询锁的状态
 *
 * 返回值：
 *   1 = 锁是开的
 *   0 = 锁是关的
 *
 * 注意：这个函数返回的是软件记录的状态，不是读 GPIO 电平。
 * 为什么？因为 GPIO 输出模式下读 ODR（输出数据寄存器）才准确，
 * 读 IDR（输入数据寄存器）可能不准（取决于外部电路）。
 * 这里用一个变量记录状态，简单可靠。
 */
uint8_t Lock_IsOpen(void)
{
    return lockIsOpen;
}

/**
 * Lock_Toggle - 翻转锁的状态
 *
 * 开→关，关→开。和按一下电灯开关一样。
 */
void Lock_Toggle(void)
{
    if (lockIsOpen) {
        Lock_Close();
    } else {
        Lock_Open();
    }
}

/**
 * Lock_OpenWithDuration - 开锁一段时间后自动上锁
 *
 * 参数：
 *   duration_ms - 开锁持续时间（毫秒）
 *
 * 这个函数会阻塞！在 duration_ms 时间内程序会卡在这里。
 * main.c 里用的是非阻塞方式（用定时器），但初学阶段先用这个理解原理。
 */
void Lock_OpenWithDuration(uint32_t duration_ms)
{
    Lock_Open();
    Delay_ms(duration_ms);
    Lock_Close();
}
