/*
 * page_env.c - 环境监控页面
 * 简洁文字 + 进度条显示
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "hal_oled.h"
#include "menu.h"
#include "font.h"
#include "hal_dht11.h"

// 菜单节点
static menu_item_t *env_root = NULL;
extern menu_item_t *menu_current;



// 主绘制函数
static void page_env_main_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Environment");
    hal_oled_line(0, 10, 127, 10);

    float temp, hum;
    char buf[32];
    int y = 13;

    // 读取传感器
    if (hal_dht11_read(&temp, &hum) == 0)
    {

        // 温度显示（文字 + 进度条）
        snprintf(buf, sizeof(buf), "Temp: %.1f C", temp);
        hal_oled_string(0, y, buf);

        // 温度进度条（下方）
        int temp_perc = (int)temp;
        temp_perc = (temp_perc > 100) ? 100 : (temp_perc < 0) ? 0
                                                              : temp_perc;
        hal_oled_draw_progress_bar(0, y + 10, 100, temp_perc, "");

        // 湿度显示（文字 + 进度条）
        y += 25;
        snprintf(buf, sizeof(buf), "Humi: %.1f %%", hum);
        hal_oled_string(0, y, buf);

        // 湿度进度条（下方）
        int hum_perc = (int)hum;
        hum_perc = (hum_perc > 100) ? 100 : (hum_perc < 0) ? 0
                                                           : hum_perc;
        hal_oled_draw_progress_bar(0, y + 10, 100, hum_perc, "");
    }
    else
    {
        // 错误提示
        hal_oled_string(20, y, "DHT11 Error!");
        hal_oled_string(20, y + 15, "Check Sensor!");
    }

    hal_oled_refresh();
}

// 事件处理
void page_env_handle_event(event_t ev)
{
    if (ev == EV_BACK)
    {
        extern void menu_back(void);
        menu_back();
    }
}

// 初始化
void page_env_init(void)
{
    static int initialized = 0;
    if (initialized)
        return;

    page_register("Environment", page_env_main_draw, page_env_handle_event);

    initialized = 1;
    printf("[UI] Environment page registered\n");
}

// 创建菜单树
menu_item_t *page_env_create_menu(void)
{
    page_env_init();

    if (env_root == NULL)
    {
        env_root = menu_create("Environment", page_env_main_draw, icon_environment_28x28);
        printf("[UI] Environment menu created\n");
    }

    return env_root;
}