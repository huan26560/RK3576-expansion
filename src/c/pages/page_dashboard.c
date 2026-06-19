#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/sysinfo.h>
#include "menu.h"
#include "hal_oled.h"
#include "system_monitor.h"
#include "hal_system.h"

extern int mqtt_is_connected(void);

typedef struct
{
    int cpu, mem, temp, net;
    time_t last_update;
    int uptime;
} remote_data_t;

// 只保留本地设备
#define DEVICE_LOCAL 0
#define DEVICE_COUNT 1

static int current_page = 0;
static int smoothed_temp = 0;

const char *page_titles[DEVICE_COUNT] = {
    "==== Main Status ===="
};

// WiFi信号dBm转百分比
static int dbm_to_percent(int dbm)
{
    if (dbm == 0 || dbm < -100 || dbm > 0)
        return 0;
    if (dbm >= -20)
        return 100;
    if (dbm <= -90)
        return 0;
    return 100 - ((abs(dbm) - 20) * 100 / 70);
}

// 格式化开机时间
static void format_uptime(char *buf, size_t size, int seconds)
{
    int days = seconds / 86400;
    int hours = (seconds % 86400) / 3600;
    int mins = (seconds % 3600) / 60;

    if (days > 0)
    {
        snprintf(buf, size, "Up:%dd%02dh", days, hours);
    }
    else if (hours > 0)
    {
        snprintf(buf, size, "Up:%dh%02dm", hours, mins);
    }
    else
    {
        snprintf(buf, size, "Up:%dm", mins);
    }
}

// 本地页面
static void draw_local(void)
{
    char buf[64];
    int y = 10;
    int label_width = 32;
    int spacing = 1;
    int bar_width = 65;

    hal_oled_string(5, 0, page_titles[DEVICE_LOCAL]);

    system_state_t state;
    system_monitor_get_state(&state);

    // 1. CPU
    int cpu_percent = state.cpu_total_usage;
    cpu_percent = (cpu_percent > 100) ? 100 : (cpu_percent < 0) ? 0
                                                                : cpu_percent;
    hal_oled_string(0, y, "CPU:");
    hal_oled_draw_progress_bar(label_width + spacing, y, bar_width, cpu_percent, "");
    y += 10;

    // 2. 内存
    int mem_percent = state.memory.used_percent;
    mem_percent = (mem_percent > 100) ? 100 : (mem_percent < 0) ? 0
                                                                : mem_percent;
    hal_oled_string(0, y, "RAM:");
    hal_oled_draw_progress_bar(label_width + spacing, y, bar_width, mem_percent, "");
    y += 10;

    // 3. 温度
    int raw_temp = state.gpu.temp_c;
    smoothed_temp = (smoothed_temp * 3 + raw_temp) / 4;
    int temp_percent = smoothed_temp;
    temp_percent = (temp_percent > 100) ? 100 : (temp_percent < 0) ? 0
                                                                   : temp_percent;
    hal_oled_string(0, y, "TMP:");
    hal_oled_draw_progress_bar(label_width + spacing, y, bar_width, temp_percent, "C");
    y += 10;

    // 4. WiFi百分比
    int wifi_dbm = hal_wifi_signal();
    if (wifi_dbm > 0)
        wifi_dbm = -wifi_dbm;
    
    snprintf(buf, sizeof(buf), "WiFi:%ddBm", wifi_dbm);
    hal_oled_string(0, y, buf);

    const char *mqtt_status = mqtt_is_connected() ? "MQTT:ON" : "MQTT:OFF";
    hal_oled_string(68, y, mqtt_status);
    y += 10;

    // 5. 日期时间 + 开机时间
    time_t now;
    struct tm *timeinfo;
    time(&now);
    timeinfo = localtime(&now);
    strftime(buf, sizeof(buf), "%m-%d %H:%M", timeinfo);
    hal_oled_string(0, y, buf);

    struct sysinfo si;
    if (sysinfo(&si) == 0)
    {
        char uptime_buf[16];
        format_uptime(uptime_buf, sizeof(uptime_buf), si.uptime);
        hal_oled_string(72, y, uptime_buf);
    }
}

void page_dashboard_draw(void)
{
    hal_oled_clear();
    draw_local(); // 只画本地页面
    hal_oled_refresh();
}

void page_dashboard_handle_event(event_t ev)
{
    // 只有一页，上下键无效，只保留返回键
    if (ev == EV_BACK)
    {
        extern void menu_back(void);
        menu_back();
    }
}

void page_dashboard_init(void)
{
    static int init = 0;
    if (init)
        return;

    extern void page_register(const char *, void (*)(void), void (*)(event_t));
    page_register("Dashboard", page_dashboard_draw, page_dashboard_handle_event);
    init = 1;
}

menu_item_t *page_dashboard_create_menu(void)
{
    page_dashboard_init();
    extern menu_item_t *menu_create(const char *, void (*)(void), const unsigned char *);
    return menu_create("Dashboard", page_dashboard_draw, NULL);
}

// 空实现，保留接口不报错
void dashboard_update_remote(const char *topic, const char *payload)
{
    // 已清空所有远程逻辑
}