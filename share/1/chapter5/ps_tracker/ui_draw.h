#ifndef UI_DRAW_H
#define UI_DRAW_H

#include <stdint.h>

typedef struct {
    int width;
    int height;
    uint32_t *pixels;
} UiCanvas;

void ui_clear(UiCanvas *canvas, uint32_t color);
void ui_fill_rect(UiCanvas *canvas, int x, int y, int w, int h, uint32_t color);
void ui_draw_rect(UiCanvas *canvas, int x, int y, int w, int h, uint32_t color);
void ui_draw_line(UiCanvas *canvas, int x0, int y0, int x1, int y1, uint32_t color);
void ui_draw_text(UiCanvas *canvas, int x, int y, const char *text, int scale, uint32_t color);
void ui_draw_checkbox(UiCanvas *canvas, int x, int y, int size, int checked, uint32_t border_color, uint32_t fill_color);

#endif
