#include "page_screensaver.h"
#include "hal_oled.h"
#include <string.h>
#include <page_interface.h>
#include <stdio.h>
#include <hal_system.h>
#include <time.h>

#define SCREENSAVER_TIMEOUT_MS 60000 // 3秒超时
#define TIME_STR_LEN 6

static int screensaver_active = 0;
static unsigned long last_activity_ms = 0;
static char time_str[TIME_STR_LEN] = {0};

// 时间获取（测试用固定值，排除时间读取问题）
static int get_system_hour(void) { return 15; }
static int get_system_minute(void) { return 48; }

// 核心：检查超时（加全日志）
static void check_screensaver_timeout(void)
{
    if (screensaver_active)
    {
        return;
    }

    unsigned long current_ms = millis();
    unsigned long idle_ms = current_ms - last_activity_ms;

    if (idle_ms >= SCREENSAVER_TIMEOUT_MS)
    {
        screensaver_active = 1;
    }
}

static void draw_screensaver_ui(void)
{
    if (!screensaver_active)
        return;
    hal_oled_clear();

    time_t now;
    struct tm *timeinfo;
    char time_buf[9] = {0}; // 容纳HH:MM:SS（8字符）+ 结束符
    time(&now);
    timeinfo = localtime(&now);
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", timeinfo); // 一次读出完整时间

    // 2. 计算紧凑居中坐标（总宽度=8*15=120px，128-120=8px余量，左右各4px）
    int total_width = 8 * 15;                    // 8个字符×紧凑步进15px=120px
    int time_x = (OLED_WIDTH - total_width) / 2; // 水平居中（(128-120)/2=4px边距）
    int time_y = (OLED_HEIGHT - 48) / 2;         // 垂直保留你原有的坐标

    hal_oled_draw_large_string(time_x, time_y, time_buf);
}

// 重置计时（加日志，看是否被意外调用）
void screensaver_reset_idle(void)
{
    last_activity_ms = millis();
    screensaver_active = 0;
}

// 事件处理（加日志）
int screensaver_handle_event(event_t ev)
{
    if (!screensaver_active)
        return 0;

    screensaver_reset_idle();
    return 1;
}

// 绘制入口（加日志）
void screensaver_draw(void)
{
    check_screensaver_timeout();
    draw_screensaver_ui();
}

// 获取激活状态
int screensaver_is_active(void) { return screensaver_active; }

// 初始化（加日志）
void screensaver_init(void)
{
    last_activity_ms = millis();
    screensaver_active = 0;
}