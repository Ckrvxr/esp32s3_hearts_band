#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BLE_DEVICE_NAME "Hearts"

extern uint32_t g_display_passkey;
extern bool g_show_passkey;
extern bool g_is_bonded;
extern bool g_ble_connected;

void Ble_Driver_Init(void);
void Ble_Driver_Send(const uint8_t *data, uint16_t len);
bool Ble_Driver_IsConnected(void);
void Ble_Driver_GetMac(char *buf, size_t len);
void Ble_Driver_ClearBonds(void);
