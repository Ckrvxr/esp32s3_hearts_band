file(GLOB U8G2_SRCS "u8g2-master/csrc/*.c")
idf_component_register(SRCS "main.c" "display.c" ${U8G2_SRCS}
                    INCLUDE_DIRS "." "u8g2-master/csrc")
