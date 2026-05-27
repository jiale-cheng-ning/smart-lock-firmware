#ifndef __KEYPAD_H
#define __KEYPAD_H

#include "stm32f4xx.h"

void KeyPad_Init(void);
char KeyPad_Scan(void);

#endif
