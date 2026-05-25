file(GLOB U8G2_SRCS "u8g2-master/csrc/*.c")

set(COMPONENT_SRCS
    "main.c"
    "display.c"
    "key.c"
    ${U8G2_SRCS}
)

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS
        "."
        "u8g2-master/csrc"
)
