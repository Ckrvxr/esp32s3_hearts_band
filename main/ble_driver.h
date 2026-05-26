#pragma once

#include <stdint.h>
#include <stdbool.h>

#define BLE_DEVICE_NAME "Hearts"

void Ble_Driver_Init(void);
void Ble_Driver_Send(const uint8_t *data, uint16_t len);
bool Ble_Driver_IsConnected(void);
