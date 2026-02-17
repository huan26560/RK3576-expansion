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

// 设备索引定义（0=本地, 1=Server, 2=香橙派5Plus, 3=泰山派）
#define DEVICE_LOCAL 0
#define DEVICE_SERVER 1
#define DEVICE_ORANGEPI 2
#define DEVICE_TAISHAN 3
#define DEVICE_ATK3588 4 // 新增
#define DEVICE_COUNT 5

static int current_page = 0;
static int smoothed_temp = 0;

remote_data_t g_remote_data = {0}; // Server
static int remote_smoothed_temp = 0;

remote_data_t g_remote_opi5p = {0}; // 香橙派5Plus
static int opi5p_smoothed_temp = 0;

remote_data_t g_remote_taishan = {0}; // 泰山派
static int taishan_smoothed_temp = 0;

remote_data_t g_remote_atk3588 = {0}; // 新增：正点原子3588
static int atk3588_smoothed_temp = 0;

const char *page_titles[DEVICE_COUNT] = {
    "==== Main Status ====",
    "=== Server Status ===",
    "==== OrangePi 5P ====",
    "==== TaiShan 3566 ===",
    "====== ATK3588 ======"};

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

// 本地页面（正点原子3588 Buildroot）
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
    int wifi_percent = dbm_to_percent(wifi_dbm);

    snprintf(buf, sizeof(buf), "WiFi:%d%%", wifi_percent);
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

// Server页面
static void draw_server(void)
{
    char buf[64];
    int y = 10;
    int label_width = 32;
    int spacing = 1;
    int bar_width = 65;

    hal_oled_string(5, 0, page_titles[DEVICE_SERVER]);

    int online = (time(NULL) - g_remote_data.last_update) < 3;

    if (!online)
    {
        hal_oled_string(0, y, "CPU:");
        hal_oled_string(label_width + spacing, y, "Offline");
        y += 10;
        hal_oled_string(0, y, "RAM:");
        hal_oled_string(label_width + spacing, y, "No Data");
        y += 10;
        hal_oled_string(0, y, "TMP:");
        hal_oled_string(label_width + spacing, y, "--");
        y += 10;
        hal_oled_string(0, y, "WiFi:0%");
        hal_oled_string(75, y, " OFF");
        y += 10;
        hal_oled_string(0, y, "Up:--");
        return;
    }

    int cpu = g_remote_data.cpu;
    cpu = (cpu > 100) ? 100 : (cpu < 0) ? 0
                                        : cpu;
    hal_oled_string(0, y, "CPU:");
    hal_oled_draw_progress_bar(label_width + spacing, y, bar_width, cpu, "");
    y += 10;

    int mem = g_remote_data.mem;
    mem = (mem > 100) ? 100 : (mem < 0) ? 0
                                        : mem;
    hal_oled_string(0, y, "RAM:");
    hal_oled_draw_progress_bar(label_width + spacing, y, bar_width, mem, "");
    y += 10;

    remote_smoothed_temp = (remote_smoothed_temp * 3 + g_remote_data.temp) / 4;
    int temp = remote_smoothed_temp;
    temp = (temp > 100) ? 100 : (temp < 0) ? 0
                                           : temp;
    hal_oled_string(0, y, "TMP:");
    hal_oled_draw_progress_bar(label_width + spacing, y, bar_width, temp, "C");
    y += 10;

    int wifi_percent = dbm_to_percent(g_remote_data.net);
    snprintf(buf, sizeof(buf), "WiFi:%d%%", wifi_percent);
    hal_oled_string(0, y, buf);
    hal_oled_string(75, y, " ON");
    y += 10;

    format_uptime(buf, sizeof(buf), g_remote_data.uptime);
    hal_oled_string(0, y, buf);
}

// 香橙派5Plus页面
static void draw_orangepi5p(void)
{
    char buf[64];
    int y = 10;
    int label_width = 32;
    int spacing = 1;
    int bar_width = 65;

    hal_oled_string(5, 0, page_titles[DEVICE_ORANGEPI]);

    int online = (time(NULL) - g_remote_opi5p.last_update) < 3;

    if (!online)
    {
        hal_oled_string(0, y, "CPU:");
        hal_oled_string(label_width + spacing, y, "Offline");
        y += 10;
        hal_oled_string(0, y, "RAM:");
        hal_oled_string(label_width + spacing, y, "No Data");
        y += 10;
        hal_oled_string(0, y, "TMP:");
        hal_oled_string(label_width + spacing, y, "--");
        y += 10;
        hal_oled_string(0, y, "WiFi:0%");
        hal_oled_string(75, y, " OFF");
        y += 10;
        hal_oled_string(0, y, "Up:--");
        return;
    }

    int cpu = g_remote_opi5p.cpu;
    cpu = (cpu > 100) ? 100 : (cpu < 0) ? 0
                                        : cpu;
    hal_oled_string(0, y, "CPU:");
    hal_oled_draw_progress_bar(label_width + spacing, y, bar_width, cpu, "");
    y += 10;

    int mem = g_remote_opi5p.mem;
    mem = (mem > 100) ? 100 : (mem < 0) ? 0
                                        : mem;
    hal_oled_string(0, y, "RAM:");
    hal_oled_draw_progress_bar(label_width + spacing, y, bar_width, mem, "");
    y += 10;

    opi5p_smoothed_temp = (opi5p_smoothed_temp * 3 + g_remote_opi5p.temp) / 4;
    int temp = opi5p_smoothed_temp;
    temp = (temp > 100) ? 100 : (temp < 0) ? 0
                                           : temp;
    hal_oled_string(0, y, "TMP:");
    hal_oled_draw_progress_bar(label_width + spacing, y, bar_width, temp, "C");
    y += 10;

    int wifi_percent = dbm_to_percent(g_remote_opi5p.net);
    snprintf(buf, sizeof(buf), "WiFi:%d%%", wifi_percent);
    hal_oled_string(0, y, buf);
    hal_oled_string(75, y, " ON");
    y += 10;

    format_uptime(buf, sizeof(buf), g_remote_opi5p.uptime);
    hal_oled_string(0, y, buf);
}

// 泰山派3566页面
static void draw_taishan(void)
{
    char buf[64];
    int y = 10;
    int label_width = 32;
    int spacing = 1;
    int bar_width = 65;

    hal_oled_string(5, 0, page_titles[DEVICE_TAISHAN]);

    int online = (time(NULL) - g_remote_taishan.last_update) < 3;

    if (!online)
    {
        hal_oled_string(0, y, "CPU:");
        hal_oled_string(label_width + spacing, y, "Offline");
        y += 10;
        hal_oled_string(0, y, "RAM:");
        hal_oled_string(label_width + spacing, y, "No Data");
        y += 10;
        hal_oled_string(0, y, "TMP:");
        hal_oled_string(label_width + spacing, y, "--");
        y += 10;
        hal_oled_string(0, y, "WiFi:0%");
        hal_oled_string(75, y, " OFF");
        y += 10;
        hal_oled_string(0, y, "Up:--");
        return;
    }

    int cpu = g_remote_taishan.cpu;
    cpu = (cpu > 100) ? 100 : (cpu < 0) ? 0
                                        : cpu;
    hal_oled_string(0, y, "CPU:");
    hal_oled_draw_progress_bar(label_width + spacing, y, bar_width, cpu, "");
    y += 10;

    int mem = g_remote_taishan.mem;
    mem = (mem > 100) ? 100 : (mem < 0) ? 0
                                        : mem;
    hal_oled_string(0, y, "RAM:");
    hal_oled_draw_progress_bar(label_width + spacing, y, bar_width, mem, "");
    y += 10;

    taishan_smoothed_temp = (taishan_smoothed_temp * 3 + g_remote_taishan.temp) / 4;
    int temp = taishan_smoothed_temp;
    temp = (temp > 100) ? 100 : (temp < 0) ? 0
                                           : temp;
    hal_oled_string(0, y, "TMP:");
    hal_oled_draw_progress_bar(label_width + spacing, y, bar_width, temp, "C");
    y += 10;

    int wifi_percent = dbm_to_percent(g_remote_taishan.net);
    snprintf(buf, sizeof(buf), "WiFi:%d%%", wifi_percent);
    hal_oled_string(0, y, buf);
    hal_oled_string(75, y, " ON");
    y += 10;

    format_uptime(buf, sizeof(buf), g_remote_taishan.uptime);
    hal_oled_string(0, y, buf);
}
// 新增：正点原子ATK3588页面
static void draw_atk3588(void)
{
    char buf[64];
    int y = 10;
    int label_width = 32;
    int spacing = 1;
    int bar_width = 65;

    hal_oled_string(5, 0, page_titles[DEVICE_ATK3588]);

    int online = (time(NULL) - g_remote_atk3588.last_update) < 5;

    if (!online)
    {
        hal_oled_string(0, y, "CPU:");
        hal_oled_string(label_width + spacing, y, "Offline");
        y += 10;
        hal_oled_string(0, y, "RAM:");
        hal_oled_string(label_width + spacing, y, "No Data");
        y += 10;
        hal_oled_string(0, y, "TMP:");
        hal_oled_string(label_width + spacing, y, "--");
        y += 10;
        hal_oled_string(0, y, "WiFi:0%");
        hal_oled_string(75, y, " OFF");
        y += 10;
        hal_oled_string(0, y, "Up:--");
        return;
    }

    int cpu = g_remote_atk3588.cpu;
    cpu = (cpu > 100) ? 100 : (cpu < 0) ? 0
                                        : cpu;
    hal_oled_string(0, y, "CPU:");
    hal_oled_draw_progress_bar(label_width + spacing, y, bar_width, cpu, "");
    y += 10;

    int mem = g_remote_atk3588.mem;
    mem = (mem > 100) ? 100 : (mem < 0) ? 0
                                        : mem;
    hal_oled_string(0, y, "RAM:");
    hal_oled_draw_progress_bar(label_width + spacing, y, bar_width, mem, "");
    y += 10;

    atk3588_smoothed_temp = (atk3588_smoothed_temp * 3 + g_remote_atk3588.temp) / 4;
    int temp = atk3588_smoothed_temp;
    temp = (temp > 100) ? 100 : (temp < 0) ? 0
                                           : temp;
    hal_oled_string(0, y, "TMP:");
    hal_oled_draw_progress_bar(label_width + spacing, y, bar_width, temp, "C");
    y += 10;

    int wifi_percent = dbm_to_percent(g_remote_atk3588.net);
    snprintf(buf, sizeof(buf), "WiFi:%d%%", wifi_percent);
    hal_oled_string(0, y, buf);
    hal_oled_string(75, y, " ON");
    y += 10;

    format_uptime(buf, sizeof(buf), g_remote_atk3588.uptime);
    hal_oled_string(0, y, buf);
}
void page_dashboard_draw(void)
{
    hal_oled_clear();
    if (current_page == DEVICE_LOCAL)
    {
        draw_local();
    }
    else if (current_page == DEVICE_SERVER)
    {
        draw_server();
    }
    else if (current_page == DEVICE_ORANGEPI)
    {
        draw_orangepi5p();
    }
    else if (current_page == DEVICE_TAISHAN)
    {
        draw_taishan();
    }
    else if (current_page == DEVICE_ATK3588)
    { // 新增
        draw_atk3588();
    }
    hal_oled_refresh();
}

void page_dashboard_handle_event(event_t ev)
{
    if (ev == EV_UP)
    {
        current_page = (current_page + 1) % DEVICE_COUNT; // 0-3循环
    }
    else if (ev == EV_DOWN)
    {
        current_page = (current_page - 1 + DEVICE_COUNT) % DEVICE_COUNT;
    }
    else if (ev == EV_BACK)
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

// 数据更新接口（移除Android）
void dashboard_update_remote(const char *topic, const char *payload)
{
    int value = atoi(payload);
    time_t now = time(NULL);

    if (strncmp(topic, "server/", 7) == 0)
    {
        g_remote_data.last_update = now;
        if (strstr(topic, "/cpu"))
            g_remote_data.cpu = value;
        else if (strstr(topic, "/mem"))
            g_remote_data.mem = value;
        else if (strstr(topic, "/temp"))
            g_remote_data.temp = value;
        else if (strstr(topic, "/net"))
            g_remote_data.net = value;
        else if (strstr(topic, "/uptime"))
            g_remote_data.uptime = value;
    }
    else if (strncmp(topic, "device/orangepi5plus/", 21) == 0)
    {
        g_remote_opi5p.last_update = now;
        if (strstr(topic, "/cpu"))
            g_remote_opi5p.cpu = value;
        else if (strstr(topic, "/mem"))
            g_remote_opi5p.mem = value;
        else if (strstr(topic, "/temp"))
            g_remote_opi5p.temp = value;
        else if (strstr(topic, "/net"))
            g_remote_opi5p.net = value;
        else if (strstr(topic, "/uptime"))
            g_remote_opi5p.uptime = value;
    }
    else if (strncmp(topic, "device/taishanpai/", 18) == 0)
    {
        g_remote_taishan.last_update = now;
        if (strstr(topic, "/cpu"))
            g_remote_taishan.cpu = value;
        else if (strstr(topic, "/mem"))
            g_remote_taishan.mem = value;
        else if (strstr(topic, "/temp"))
            g_remote_taishan.temp = value;
        else if (strstr(topic, "/net"))
            g_remote_taishan.net = value;
        else if (strstr(topic, "/uptime"))
            g_remote_taishan.uptime = value;
    }

    else if (strncmp(topic, "device/atk3588/", 15) == 0)
    {
        g_remote_atk3588.last_update = now;  
        if (strstr(topic, "/cpu"))
            g_remote_atk3588.cpu = value;   
        else if (strstr(topic, "/mem"))
            g_remote_atk3588.mem = value;   
        else if (strstr(topic, "/temp"))
            g_remote_atk3588.temp = value;  
        else if (strstr(topic, "/net"))
            g_remote_atk3588.net = value;   
        else if (strstr(topic, "/uptime"))
            g_remote_atk3588.uptime = value; 
    }
    // Android部分已移除
}