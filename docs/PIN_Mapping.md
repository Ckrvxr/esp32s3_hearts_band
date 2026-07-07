# PIN Mapping

## Display SSD1315 IIC

| Device Side | IO MUX Pin | ESP32 Side |
|--------|------------|-------------|
| SSD1315 IIC SCL | PIN 3 | ESP32 IIC SCL |
| SSD1315 IIC SDA | PIN 4 | ESP32 IIC SDA |

## Key GPIO

| Key Side| Function | IO MUX Pin | ESP32 Side |
|-----|----------|------------|-------------|
| Key 1 | CANCEL | PIN 5 | ESP32 GPIO |
| Key 2 | CONFIRM | PIN 6 | ESP32 GPIO |
| Key 3 | DOWN | PIN 7 | ESP32 GPIO |
| Key 4 | UP | PIN 15 | ESP32 GPIO |

## MAX30100 Heart Rate Sensor IIC

| Signal | IO MUX Pin | ESP32 Side |
|--------|------------|-------------|
| MAX30100 IIC SCL | PIN 17 | ESP32 IIC SCL |
| MAX30100 IIC SDA | PIN 18 | ESP32 IIC SDA |

## Buzzer (Passive, PWM)

| Signal | GPIO | PWM Peripheral |
|--------|------|----------------|
| PWM Out | GPIO 9 | LEDC_CHANNEL_0 |

## CH343 (CSV Data Output)

| Signal | UART | GPIO | Baud Rate | Data Format |
|--------|------|------|-----------|-------------|
| TX (ESP32 → PC) | UART0 | GPIO43 | 115200 | `IR,RED,<ir_raw>,<red_raw>\n` (50Hz) |
| RX (PC → ESP32) | UART0 | GPIO44 | 115200 | |

## Native USB (Console / ESP_LOGI)

| Signal | Interface |
|--------|-----------|
| USB Serial/JTAG | Built-in USB (console output) |
