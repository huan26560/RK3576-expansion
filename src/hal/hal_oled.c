/*
 * hal_oled.c - SSD1306 OLED 驱动（I2C）
 * 适配哈吉米3 I2C-3，地址 0x3C
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include "hal_oled.h"
#include <fcntl.h>
#include <sys/syscall.h>
#include <pthread.h>
#include <font.h>
#define OLED_ADDR 0x3C
#define I2C_BUS "/dev/i2c-7"
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

static int i2c_fd = -1;
static pthread_mutex_t i2c_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint8_t oled_buffer[OLED_WIDTH * OLED_HEIGHT / 8];
// 内部函数前置声明
static void oled_send_command(uint8_t cmd);
static void oled_send_data(const uint8_t *data, size_t len);

int hal_oled_init(void)
{
    printf("正在初始化 OLED...\n");

    // 2. 打开I2C设备
    i2c_fd = open(I2C_BUS, O_RDWR);
    if (i2c_fd < 0)
    {
        printf("错误：无法打开 I2C 设备 %s\n", I2C_BUS);
        return -1;
    }

    // 3. 设置从地址
    if (ioctl(i2c_fd, I2C_SLAVE, OLED_ADDR) < 0)
    {
        printf("错误：无法设置 I2C 从地址 0x3C\n");
        close(i2c_fd);
        i2c_fd = -1;
        return -1;
    }

    // 4. SSD1306 初始化序列（保持原有）
    uint8_t init[] = {
        0x00, 0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xCF,
        0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF};
    write(i2c_fd, init, sizeof(init));

    // 5. 清空屏幕
    memset(oled_buffer, 0, sizeof(oled_buffer));
    hal_oled_clear();
    hal_oled_refresh();

    printf("OLED 初始化完成\n");
    return 0;
}

// 改为 static，强制通过 hal_oled_refresh 刷新
static void oled_send_command(uint8_t cmd)
{
    pthread_mutex_lock(&i2c_mutex);
    if (i2c_fd < 0)
    {
        pthread_mutex_unlock(&i2c_mutex);
        return;
    }
    uint8_t buf[2] = {0x00, cmd};
    write(i2c_fd, buf, 2);
    pthread_mutex_unlock(&i2c_mutex);
}

static void oled_send_data(const uint8_t *data, size_t len)
{
    pthread_mutex_lock(&i2c_mutex);
    if (i2c_fd < 0)
    {
        pthread_mutex_unlock(&i2c_mutex);
        return;
    }
    uint8_t buf[129];
    buf[0] = 0x40;
    memcpy(buf + 1, data, len > 128 ? 128 : len);
    write(i2c_fd, buf, len + 1);
    pthread_mutex_unlock(&i2c_mutex);
}

void hal_oled_clear(void)
{
    memset(oled_buffer, 0, sizeof(oled_buffer));
}

void hal_oled_pixel(int x, int y, int on)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT)
        return;
    int index = x + (y / 8) * OLED_WIDTH;
    if (on)
        oled_buffer[index] |= (1 << (y % 8));
    else
        oled_buffer[index] &= ~(1 << (y % 8));
}
void hal_oled_char(int x, int y, char c)
{
    if (c < 32 || c > 127)
        return;
    for (int i = 0; i < 6; i++)
    {
        for (int j = 0; j < 8; j++)
        {
            hal_oled_pixel(x + i, y + j, font_small_raw[c - 32][i] & (1 << j));
        }
    }
}

void hal_oled_string(int x, int y, const char *str)
{
    while (*str)
    {
        hal_oled_char(x, y, *str++);
        x += 6;
    }
}
void hal_oled_draw_progress_bar(int x, int y, int width, int percent, const char *label)
{
    // 绘制标签
    hal_oled_string(x, y, label);

    // 进度条位置
    int bar_y = y;
    int bar_height = 6;

    // 绘制边框
    for (int i = x; i <= x + width; i++)
    {
        hal_oled_pixel(i, bar_y, 1);
        hal_oled_pixel(i, bar_y + bar_height, 1);
    }
    for (int i = bar_y; i <= bar_y + bar_height; i++)
    {
        hal_oled_pixel(x, i, 1);
        hal_oled_pixel(x + width, i, 1);
    }

    // 填充进度（留出1像素边框）
    int fill_width = (width - 2) * percent / 100;
    for (int i = 0; i < fill_width; i++)
    {
        for (int j = 1; j < bar_height; j++)
        {
            hal_oled_pixel(x + 1 + i, bar_y + j, 1);
        }
    }

    // 右侧显示百分比
    char percent_str[8];
    snprintf(percent_str, sizeof(percent_str), "%d%%", percent);
    hal_oled_string(x + width + 4, bar_y - 1, percent_str);
}
void hal_oled_refresh(void)
{
    if (i2c_fd < 0)
        return;

    pthread_mutex_lock(&i2c_mutex);

    // 使用更大的数据块传输，减少I2C命令开销
    for (int page = 0; page < 8; page++)
    {
        // 设置页地址和列地址（一次写入）
        uint8_t cmd[3] = {0x00, 0xB0 + page, 0x00};
        write(i2c_fd, cmd, 3);
        write(i2c_fd, (uint8_t[]){0x00, 0x10}, 2); // 高列地址

        // 发送整页数据（128字节），减少分块次数
        uint8_t data[129] = {0x40};
        memcpy(&data[1], &oled_buffer[page * 128], 128);
        write(i2c_fd, data, 129);
    }

    pthread_mutex_unlock(&i2c_mutex);
}

void hal_oled_line(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1)
    {
        hal_oled_pixel(x0, y0, 1);
        if (x0 == x1 && y0 == y1)
            break;
        e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

// 在 hal_oled_line() 函数之后，hal_oled_cleanup() 之前添加：

// 绘制空心矩形
void hal_oled_rect(int x, int y, int width, int height, int on)
{
    if (x < 0 || y < 0 || x >= OLED_WIDTH || y >= OLED_HEIGHT)
        return;

    // 边界检查
    if (x + width > OLED_WIDTH)
        width = OLED_WIDTH - x;
    if (y + height > OLED_HEIGHT)
        height = OLED_HEIGHT - y;

    // 绘制四条边
    for (int i = 0; i < width; i++)
    {
        hal_oled_pixel(x + i, y, on);              // 上边
        hal_oled_pixel(x + i, y + height - 1, on); // 下边
    }
    for (int i = 0; i < height; i++)
    {
        hal_oled_pixel(x, y + i, on);             // 左边
        hal_oled_pixel(x + width - 1, y + i, on); // 右边
    }
}

// 绘制填充矩形
void hal_oled_fill_rect(int x, int y, int width, int height, int on)
{
    if (x < 0 || y < 0 || x >= OLED_WIDTH || y >= OLED_HEIGHT)
        return;

    // 边界检查
    if (x + width > OLED_WIDTH)
        width = OLED_WIDTH - x;
    if (y + height > OLED_HEIGHT)
        height = OLED_HEIGHT - y;

    // 填充整个矩形
    for (int i = 0; i < width; i++)
    {
        for (int j = 0; j < height; j++)
        {
            hal_oled_pixel(x + i, y + j, on);
        }
    }
}

void hal_oled_draw_icon(int x, int y, int width, int height, const unsigned char *icon_data)
{
    int bytes_per_row = (width + 7) / 8;

    for (int row = 0; row < height; row++)
    {
        for (int byte_idx = 0; byte_idx < bytes_per_row; byte_idx++)
        {
            unsigned char byte = icon_data[row * bytes_per_row + byte_idx];

            for (int bit = 0; bit < 8; bit++)
            {
                if (byte & (0x80 >> bit))
                {
                    int px = x + byte_idx * 8 + bit;
                    int py = y + row;
                    if (px < OLED_WIDTH && py < OLED_HEIGHT)
                        hal_oled_pixel(px, py, 1);
                }
            }
        }
    }
}
void hal_oled_draw_large_char(int x0, int y0, char ch) {
    int idx;
    if (ch >= '0' && ch <= '9')
        idx = ch - '0';
    else if (ch == ':')
        idx = 10;
    else
        return;  // 不支持的字符

    // 遍历字符的每一行
    for (int y = 0; y < 48; y++) {
        // 遍历每一列（每行3字节，共24像素）
        for (int x = 0; x < 24; x++) {
            int byte_idx = x / 8;          // 当前列属于该行的第几个字节
            int bit = 7 - (x % 8);          // 高位对应左边像素（根据您的数据格式）
            if (font[idx][y][byte_idx] & (1 << bit)) {
                hal_oled_pixel(x0 + x, y0 + y, 1);  // 点亮像素
            }
            // 若不需要清除背景，可省略else；如需清除可调用 hal_oled_pixel(...,0)
        }
    }
}

void hal_oled_draw_large_string(int x0, int y0, const char *str) {
    while (*str) {
        hal_oled_draw_large_char(x0, y0, *str);
        x0 += CHAR_STEP;   // 使用新的步进宽度
        str++;
    }
}
void hal_oled_cleanup(void)
{
    pthread_mutex_lock(&i2c_mutex);
    if (i2c_fd >= 0)
    {
        close(i2c_fd);
        i2c_fd = -1;
    }
    pthread_mutex_unlock(&i2c_mutex);
}