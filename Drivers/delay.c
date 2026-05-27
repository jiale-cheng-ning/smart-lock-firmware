/**
 * SysTick 延时函数
 *
 * SysTick 是 ARM Cortex-M 内核里的一个 24 位倒计数定时器。
 * 它数一次就是 1 个时钟周期。配置好之后，我们可以用它来实现精确延时。
 *
 * 这个文件你只需要知道两个函数：
 *   Delay_ms(n)  —— 延时 n 毫秒
 *   Delay_us(n)  —— 延时 n 微秒
 */

#include "delay.h"

/* 系统时钟频率（SystemCoreClock 是 STM32 库定义的全局变量）
 * STM32F411 默认 16MHz（HSI），如果配了 PLL 可以到 100MHz */
static uint32_t fac_us = 0;  /* 微秒分频因子 */
static uint32_t fac_ms = 0;  /* 毫秒分频因子 */

/**
 * SysTick_Init - 初始化 SysTick 定时器
 *
 * SysTick 的时钟源是系统时钟（HCLK）。
 * HCLK = 16MHz 时，每微秒计数 16 次。
 *
 * 这个函数算出"1微秒要数多少次"，存到 fac_us 里，
 * 后面 Delay_us 就用这个值来倒计数。
 */
void SysTick_Init(void)
{
    /* SystemCoreClock / 1000000 = 每微秒的时钟周期数
     * 例如 16MHz 时：16000000 / 1000000 = 16
     * 就是说 1 微秒内 SysTick 要数 16 次 */
    fac_us = SystemCoreClock / 1000000;
    fac_ms = fac_us * 1000;  /* 1 毫秒 = 1000 微秒 */
}

/**
 * Delay_us - 微秒级延时
 *
 * 原理：
 *   1. 把 fac_us * nus 装进 SysTick 的重装载寄存器
 *   2. 清零当前值寄存器（从头开始数）
 *   3. 启动 SysTick
 *   4. 等待计数到 0（COUNTFLAG 位变 1）
 *   5. 关闭 SysTick
 *
 * 最大延时：2^24 / fac_us 微秒 ≈ 1048ms（24位计数器）
 * 所以 Delay_us 最多传 1048576 微秒（约 1 秒）
 */
void Delay_us(uint32_t nus)
{
    uint32_t temp;

    SysTick->LOAD = fac_us * nus;         /* 重装载值 = 每微秒计数 × 微秒数 */
    SysTick->VAL  = 0x00;                  /* 清零当前值，从头开始数 */
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;  /* 启动计数 */

    do {
        temp = SysTick->CTRL;              /* 读控制寄存器 */
    } while ((temp & 0x01) && !(temp & (1 << 16)));
    /* 循环条件：ENABLE 为 1 且 COUNTFLAG 为 0
     * 也就是说：还在数，还没数完 → 继续等
     * 数完了（COUNTFLAG=1）→ 退出循环 */

    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;  /* 关闭定时器 */
    SysTick->VAL  = 0x00;                         /* 清零 */
}

/**
 * Delay_ms - 毫秒级延时
 *
 * SysTick 是 24 位计数器，一次最多数 2^24 次。
 * 如果系统时钟是 100MHz，1ms 需要数 100000 次，
 * 所以一次最多延时 2^24 / 100000 ≈ 167ms。
 *
 * 为了支持更长的延时，我们分段：每次最多延时 1000ms，
 * 超过 1000ms 就循环多次。
 */
void Delay_ms(uint32_t nms)
{
    uint32_t temp;

    while (nms) {
        /* 本次要延时的毫秒数（最多 1000ms 或剩余值） */
        uint32_t t = (nms > 1000) ? 1000 : nms;

        SysTick->LOAD = (uint32_t)fac_ms * t;
        SysTick->VAL  = 0x00;
        SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;

        do {
            temp = SysTick->CTRL;
        } while ((temp & 0x01) && !(temp & (1 << 16)));

        SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
        SysTick->VAL  = 0x00;

        nms -= t;
    }
}
