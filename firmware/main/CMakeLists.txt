file(GLOB FONT_SRCS "fonts/*.c")

idf_component_register(
    SRCS "main.c" "display.c" "hero_size.c" "colortest.c" ${FONT_SRCS}
    INCLUDE_DIRS "." "fonts"
)
