#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BLE_DEVICE_NAME "409 Hearts Bond"

extern bool g_ble_connected;

void Ble_Driver_Init(void);
void Ble_Driver_Send(const uint8_t *data, uint16_t len);
bool Ble_Driver_IsConnected(void);
void Ble_Driver_GetMac(char *buf, size_t len);
void Ble_Driver_ClearBonds(void);
void Ble_Driver_SendAck(const char *ack_type, const char *uuid, const char *type, int time);
void Ble_Driver_ReportHealth(uint8_t hr, uint8_t spo2);
