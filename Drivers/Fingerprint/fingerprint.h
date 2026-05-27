#ifndef __FINGERPRINT_H
#define __FINGERPRINT_H

#include "stm32f4xx.h"

void Fingerprint_Init(void);
uint8_t Fingerprint_IsFingerPressed(void);
uint16_t Fingerprint_Match(void);
uint16_t Fingerprint_Enroll(uint16_t id);

#endif
