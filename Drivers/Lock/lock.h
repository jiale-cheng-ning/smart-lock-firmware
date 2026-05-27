#ifndef __LOCK_H
#define __LOCK_H

#include "stm32f4xx.h"

void Lock_Init(void);
void Lock_Open(void);
void Lock_Close(void);
uint8_t Lock_IsOpen(void);

#endif
