/*
 * page_env.c - 天气UI页面（仅负责UI绘制，无线程逻辑）
 * 新增：显示拼音地址（湿度下方、时间上方）
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "hal_oled.h"
#include "menu.h"
#include "font.h"
#include "hal_dht11.h"
#include "thread.h" // 引入线程中心的接口

/************************** 配置 **************************/
#define TAB_COUNT 2
#define TAB_LOCAL 0
#define TAB_NET_WEATHER 1
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define WEATHER_ICON_SIZE 28

/************************** 全局变量 **************************/
// 天气图标定义（保持不变）
extern const unsigned char icon_weather_clear[];
extern const unsigned char icon_weather_cloudy[];
extern const unsigned char icon_weather_rain[];
extern const unsigned char icon_weather_snow[];
extern const unsigned char icon_weather_fog[];
extern const unsigned char icon_weather_thunderstorm[];
extern const unsigned char icon_weather_unknown[];

static menu_item_t *env_root = NULL;
static int current_tab = TAB_NET_WEATHER;
extern menu_item_t *menu_current;

/************************** 工具函数 **************************/
static void hal_oled_fill_rect_simple(int x1, int y1, int x2, int y2)
{
    for (int y = y1; y <= y2; y++)
    {
        hal_oled_line(x1, y, x2, y);
    }
}

static void get_current_datetime(char *buf, int buf_size)
{
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buf, buf_size, "%Y/%m/%d %H:%M", t);
}

static const unsigned char *weather_code_to_icon(int code)
{
    if (code == 0)
        return icon_weather_clear;
    if (code == 1 || code == 2 || code == 3)
        return icon_weather_cloudy;
    if (code >= 45 && code <= 48)
        return icon_weather_fog;
    if (code >= 51 && code <= 67)
        return icon_weather_rain;
    if (code >= 71 && code <= 86)
        return icon_weather_snow;
    if (code >= 95)
        return icon_weather_thunderstorm;
    return icon_weather_unknown;
}

/************************** UI绘制 **************************/
static void draw_tab_indicator(void)
{
    const char *title = "Weather";
    int title_x = (OLED_WIDTH - strlen(title) * 6) / 2;
    int y = 0;

    hal_oled_string(title_x, y, (char *)title);
    hal_oled_line(0, 9, OLED_WIDTH - 1, 9);
}

static void draw_local_dht11(void)
{
    float temp, hum;
    char buf[32];
    char datetime[32];

    get_current_datetime(datetime, sizeof(datetime));
    hal_oled_string(15, 56, datetime);

    int y = 15;
    if (hal_dht11_read(&temp, &hum) == 0)
    {
        snprintf(buf, sizeof(buf), "Temp: %.1f C", temp);
        hal_oled_string(10, y, buf);

        y += 15;
        snprintf(buf, sizeof(buf), "Humi: %.1f %%", hum);
        hal_oled_string(10, y, buf);
    }
    else
    {
        hal_oled_string(20, y, "DHT11 Error!");
    }
}

static void draw_network_weather(void)
{
    char buf[32];
    char datetime[32];
    char address[64] = {0}; // 新增：拼音地址缓冲区
    network_weather_t local_weather;
    char lat[16] = {0}, lon[16] = {0};

    // 从线程中心获取数据（线程安全）
    get_weather_data(&local_weather);
    get_weather_location(lat, lon, sizeof(lat));
    get_weather_address(address, sizeof(address)); // 新增：获取拼音地址

    get_current_datetime(datetime, sizeof(datetime));

    if (local_weather.is_valid)
    {
        // 左侧：图标 + 天气文字
        int icon_x = 8;
        int icon_y = 12;
        const unsigned char *icon = weather_code_to_icon(local_weather.weather_code);
        hal_oled_draw_icon(icon_x, icon_y, WEATHER_ICON_SIZE, WEATHER_ICON_SIZE, icon);

        int text_y = icon_y + WEATHER_ICON_SIZE + 2;
        hal_oled_string(icon_x, text_y, local_weather.weather_text);

        // 右侧：Temp + Humi（带前缀）
        int right_x = 55;
        snprintf(buf, sizeof(buf), "Temp: %.1f C", local_weather.temp);
        hal_oled_string(right_x, icon_y, buf);

        snprintf(buf, sizeof(buf), "Humi: %.1f%%", local_weather.humi);
        hal_oled_string(right_x, 24, buf);

        // 新增：拼音地址换行显示（城市一行，省份一行）
        int addr_y1 = 36; // 第一行Y坐标（城市）
        int addr_y2 = 46; // 第二行Y坐标（省份）

        // 复制地址到临时变量，避免修改原数据
        char addr_temp[64] = {0};
        strncpy(addr_temp, address, sizeof(addr_temp) - 1);

        // 按逗号分割地址
        char *city = addr_temp;
        char *province = strchr(addr_temp, ','); // 找逗号位置

        if (province != NULL)
        {
            *province = '\0'; // 把逗号替换成结束符，分割成两个字符串
            province++;       // 跳过逗号，指向省份开头
            // 跳过省份前面的空格（如果有）
            while (*province == ' ')
                province++;
        }

        // 第一行：显示城市（居中）
        if (strlen(city) > 0)
        {
            int city_x = (OLED_WIDTH - strlen(city) * 1) / 2;
            if (city_x < 0)
                city_x = 0;
            hal_oled_string(city_x, addr_y1, city);
        }

        // 第二行：显示省份（居中）
        if (province != NULL && strlen(province) > 0)
        {
            int prov_x = (OLED_WIDTH - strlen(province) * 1) / 2;
            if (prov_x < 0)
                prov_x = 0;
            hal_oled_string(prov_x, addr_y2, province);
        }

        // 底部：日期时间
        hal_oled_string(15, 56, datetime);
    }
    else
    {
        // 加载中/错误提示
        hal_oled_string(10, 20, "Loading...");
        hal_oled_string(10, 35, "Please wait");

        char loc_buf[40];
        snprintf(loc_buf, sizeof(loc_buf), "Lat:%s Lon:%s", lat, lon);
        hal_oled_string(10, 50, loc_buf);
    }
}

static void page_env_main_draw(void)
{
    hal_oled_clear();

    draw_tab_indicator();

    switch (current_tab)
    {
    case TAB_LOCAL:
        draw_local_dht11();
        break;
    case TAB_NET_WEATHER:
        draw_network_weather();
        break;
    default:
        current_tab = TAB_NET_WEATHER;
        draw_network_weather();
        break;
    }
}

/************************** 事件处理 **************************/
void page_env_handle_event(event_t ev)
{
    if (menu_current != NULL && strcmp(menu_current->name, "weather") == 0)
    {
        switch (ev)
        {
        case EV_UP:
            current_tab = (current_tab - 1 + TAB_COUNT) % TAB_COUNT;
            page_env_main_draw();
            break;

        case EV_DOWN:
            current_tab = (current_tab + 1) % TAB_COUNT;
            page_env_main_draw();
            break;

        case EV_ENTER:
            if (current_tab == TAB_NET_WEATHER)
            {
                // 调用线程中心的刷新接口（非阻塞）
                trigger_weather_refresh();
                page_env_main_draw(); // 立即更新UI显示Loading
            }
            break;

        case EV_BACK:
            extern void menu_back(void);
            current_tab = TAB_NET_WEATHER;
            menu_back();
            break;
        }
        return;
    }
}

/************************** 页面注册 **************************/

void page_env_init(void)
{
    static int initialized = 0;
    if (initialized)
        return;

    // 仅注册页面，线程初始化交给threads.c
    page_register("weather", page_env_main_draw, page_env_handle_event);
    initialized = 1;
}

menu_item_t *page_env_create_menu(void)
{
    page_env_init();
    if (env_root == NULL)
    {
        env_root = menu_create("weather", page_env_main_draw, icon_environment_28x28);
    }
    return env_root;
}