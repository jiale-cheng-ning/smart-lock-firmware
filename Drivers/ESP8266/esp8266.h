#ifndef __ESP8266_H
#define __ESP8266_H

#include "stm32f4xx.h"

void ESP8266_Init(void);
uint8_t ESP8266_ConnectWiFi(const char *ssid, const char *password);
uint8_t ESP8266_SendEvent(const char *event);
uint8_t ESP8266_GetIP(char *ipBuf, uint8_t bufSize);

/* 配网用：直接发送 AT 指令（WiFi 配置模式需要） */
uint8_t ESP_SendATCommand(const char *cmd, const char *expected, uint32_t timeout);

#endif
