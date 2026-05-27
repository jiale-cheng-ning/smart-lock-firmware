#ifndef __MAIN_H
#define __MAIN_H

#include "stm32f4xx.h"

/* 系统配置 */
#define MAX_PASSWORD_LEN    8
#define MAX_ATTEMPTS        3
#define LOCKOUT_DURATION    30000
#define AUTO_LOCK_DELAY     5000

/* GPIO 引脚定义 */
#define LOCK_PIN            GPIO_Pin_1
#define LOCK_PORT           GPIOA
#define LOCK_RCC            RCC_AHB1Periph_GPIOA

#define LED_PIN             GPIO_Pin_13
#define LED_PORT            GPIOC
#define LED_RCC             RCC_AHB1Periph_GPIOC

#endif
