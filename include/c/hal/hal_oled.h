#ifndef HAL_OLED_H
#define HAL_OLED_H
// 从hal_oled.c中同步宏定义
#define OLED_WIDTH  128
#define OLED_HEIGHT 64
// 小字体尺寸（匹配hal_oled_char实现）
#define CHAR_SMALL_WIDTH  6
#define CHAR_SMALL_HEIGHT 8
// 超大字体尺寸（自定义16x16）
#define CHAR_BIG_WIDTH   24
#define CHAR_BIG_HEIGHT  48
#define CHAR_STEP 15
int hal_oled_init(void);
void hal_oled_clear(void);
void hal_oled_pixel(int x, int y, int on);
void hal_oled_char(int x, int y, char c);
void hal_oled_string(int x, int y, const char *str);
void hal_oled_refresh(void);
void hal_oled_cleanup(void);
void hal_oled_line(int x0, int y0, int x1, int y1);  // 添加这行
void hal_oled_draw_progress_bar(int x, int y, int width, int percent, const char *label);
void hal_oled_rect(int x, int y, int width, int height, int on);
void hal_oled_fill_rect(int x, int y, int width, int height, int on);
void hal_oled_draw_icon(int x, int y, int width, int height, const unsigned char *icon_data);
void hal_oled_draw_large_char(int x0, int y0, char ch);
void hal_oled_draw_large_string(int x0, int y0, const char *str);

#endif
