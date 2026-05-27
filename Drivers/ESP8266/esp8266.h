#ifndef __ESP8266_H
#define __ESP8266_H

#include "stm32f4xx.h"

void ESP8266_Init(void);
uint8_t ESP8266_ConnectWiFi(const char *ssid, const char *password);
uint8_t ESP8266_SendEvent(const char *event);

#endif
