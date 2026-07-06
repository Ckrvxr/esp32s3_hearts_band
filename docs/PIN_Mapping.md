# PIN Mapping

## Display SSD1315 IIC

| Device Side | IO MUX Pin | ESP32 Side |
|--------|------------|-------------|
| SSD1315 IIC SCL | PIN 4 | ESP32 IIC SCL |
| SSD1315 IIC SDA | PIN 5 | ESP32 IIC SDA |

## Key GPIO

| Key Side| Function | IO MUX Pin | ESP32 Side |
|-----|----------|------------|-------------|
| Key 1 | CANCEL | PIN 6 | ESP32 GPIO |
| Key 2 | CONFIRM | PIN 7 | ESP32 GPIO |
| Key 3 | DOWN | PIN 15 | ESP32 GPIO |
| Key 4 | UP | PIN 16 | ESP32 GPIO |

## MAX30100 Heart Rate Sensor IIC

| Signal | IO MUX Pin | ESP32 Side |
|--------|------------|-------------|
| MAX30100 IIC SCL | PIN 18 | ESP32 IIC SCL |
| MAX30100 IIC SDA | PIN 8 | ESP32 IIC SDA |

## UART1 (Raw Data Output to PC)

| Signal | IO MUX Pin | Baud Rate | Data Format (CSV) |
|--------|------------|-----------|-------------|
| TX (ESP32 → PC USB-UART) | GPIO 21 | 921600 | `RAW,<count>,<ir_raw>,<y_dc>\n` |
