file(GLOB U8G2_SRCS "u8g2-master/csrc/*.c")

set(COMPONENT_SRCS
    "main.c"
    "display.c"
    "key.c"
    "max30100.c"
    "ble.c"
    "ppg.c"
    "ppg_v2.c"
    ${U8G2_SRCS}
)

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS
        "."
        "u8g2-master/csrc"
    REQUIRES bt nvs_flash esp_driver_gpio esp_driver_uart
    PRIV_REQUIRES esp_driver_i2c
)
