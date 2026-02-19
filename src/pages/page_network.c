#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "hal_oled.h"
#include "hal_system.h"
#include "page_interface.h"
#include "menu.h"
#include "font.h"
#include "network_monitor.h"

// ==================== 核心变量（仅保留网络监控相关）====================
static int lan_scroll_offset = 0;
static const char *last_drawn_lan_menu = NULL;
#define OLED_WIDTH 128
#define CHAR_WIDTH 6

static int show_ping_result = 0;
static char ping_result[64] = {0};
static int selected_device = 0;
static int list_selected = 0;
static int status_page_index = 0;

static menu_item_t *net_root = NULL;
static menu_item_t *list_node = NULL;
static menu_item_t *status_node = NULL;
static menu_item_t *lan_scan_node = NULL;

extern menu_item_t *menu_current;
extern unsigned long millis(void);

// ==================== 工具函数（仅保留Ping测试）====================
static int ping_device(const char *ip)
{
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "ping -c 1 -W 2 %s 2>&1 | grep 'time=' | cut -d= -f4 | cut -d' ' -f1", ip);
    char result[32] = {0};
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    int n = fread(result, 1, sizeof(result)-1, fp);
    result[n] = 0;
    pclose(fp);
    return (result[0] >= '0' && result[0] <= '9') ? atoi(result) : -1;
}

// ==================== 页面绘制（彻底删除配网相关）====================
// 网络主页面：显示WiFi连接状态、固定IP、LAN设备数
static void page_network_root_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Network Manager");
    hal_oled_line(0, 10, 127, 10);
    
    network_state_t *ns = &g_network_state;
    char ssid[32] = {0}, ip[32] = {0};
    int connected = 0, count = 0;
    
    pthread_mutex_lock(&ns->lock);
    strncpy(ssid, ns->wifi_ssid, sizeof(ssid)-1);
    strncpy(ip, ns->wifi_ip, sizeof(ip)-1);
    connected = ns->wifi_connected;
    count = ns->lan_device_count;
    pthread_mutex_unlock(&ns->lock);

    char buf[64];
    snprintf(buf, sizeof(buf), "WiFi: %s", connected ? "Connected" : "Disconnected");
    hal_oled_string(0, 20, buf);
    
    if (ip[0]) {
        snprintf(buf, sizeof(buf), "IP: %s", ip); // 显示你的固定IP
        hal_oled_string(0, 30, buf);
    }
    if (ssid[0]) {
        snprintf(buf, sizeof(buf), "SSID: %.15s", ssid);
        hal_oled_string(0, 40, buf);
    }
    snprintf(buf, sizeof(buf), "LAN: %d devices", count);
    hal_oled_string(0, 50, buf);
    
    hal_oled_refresh();
}

// 网络工具菜单：仅保留Status、LAN Scan（删掉WiFi Config）
static void page_network_list_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Network Tools");
    hal_oled_line(0, 10, 127, 10);
    
    const char *items[] = {"Status", "LAN Scan"}; // 仅2个选项
    int y = 18;
    for (int i=0; i<2; i++) {
        char line[32];
        snprintf(line, sizeof(line), "%s %s", i==list_selected ? ">" : " ", items[i]);
        hal_oled_string(0, y, line);
        y += 10;
    }
    
    hal_oled_refresh();
}

// 网络状态页面：显示SSID、固定IP、信号强度
// 网络状态页面：显示SSID、固定IP、信号强度（修复信号不显示问题）
static void page_network_status_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Network Status");
    hal_oled_line(0, 10, 127, 10);
    
    network_state_t *ns = &g_network_state;
    char buf[128];
    int y = 18;
    int signal = -999; // 初始值标记无信号
    
    pthread_mutex_lock(&ns->lock);
    if (status_page_index == 0) {
        // 1. 显示SSID
        snprintf(buf, sizeof(buf), "SSID:%.25s", ns->wifi_ssid[0] ? ns->wifi_ssid : "N/A");
        hal_oled_string(0, y, buf);
        y +=10;
        
        // 2. 显示IP
        snprintf(buf, sizeof(buf), "IP:%.30s", ns->wifi_ip[0] ? ns->wifi_ip : "N/A");
        hal_oled_string(0, y, buf);
        y +=10;
        
        // 3. 优先用结构体的信号值，无值则通过系统命令获取
        if (ns->wifi_signal != 0) {
            signal = ns->wifi_signal;
        } else {
            // 系统命令获取真实WiFi信号强度（鲁棒性更高）
            FILE *fp = popen("iwconfig wlan0 | grep 'Signal level' | awk '{print $4}' | cut -d= -f2", "r");
            if (fp) {
                char sig_buf[16] = {0};
                fgets(sig_buf, sizeof(sig_buf), fp);
                if (sig_buf[0] != 0) {
                    signal = atoi(sig_buf);
                }
                pclose(fp);
            }
        }
        
        // 4. 格式化显示信号强度（-dBm，范围通常-100~0，数值越大信号越好）
        if (signal != -999) {
            snprintf(buf, sizeof(buf), "Signal:%d dBm", signal);
        } else {
            snprintf(buf, sizeof(buf), "Signal:N/A");
        }
        hal_oled_string(0, y, buf);
        y +=10;
    } else {
        // 显示网关信息
        FILE *fp = popen("ip route | grep default | awk '{print \"GW:\"$3}'", "r");
        if (fp) {
            fgets(buf, sizeof(buf), fp);
            hal_oled_string(0, y, buf);
            y +=10;
            pclose(fp);
        }
    }
    pthread_mutex_unlock(&ns->lock);
    
    hal_oled_refresh();
}
// LAN扫描页面：显示设备列表、支持Ping测试
static void page_network_lan_draw(void)
{
    if (last_drawn_lan_menu != menu_current->name && !strcmp(menu_current->name, "LAN Scan")) {
        selected_device = 0;
        lan_scroll_offset = 0;
    }
    last_drawn_lan_menu = menu_current->name;

    if (show_ping_result) {
        hal_oled_clear();
        hal_oled_string(0, 0, "Ping Result");
        hal_oled_line(0, 10, 127, 10);
        
        int x = (OLED_WIDTH - strlen(ping_result) * CHAR_WIDTH)/2;
        hal_oled_string(x, 30, ping_result);
        hal_oled_string(0, 55, "BACK: Return");
        
        hal_oled_refresh();
        return;
    }
    hal_oled_clear();
    hal_oled_string(0, 0, "LAN Devices");
    hal_oled_line(0, 10, 127, 10);
    
    network_state_t *ns = &g_network_state;
    char devices[20][32] = {0};
    int count = 0;
    
    pthread_mutex_lock(&ns->lock);
    count = ns->lan_device_count;
    for (int i=0; i<count; i++) strncpy(devices[i], ns->lan_devices[i], 31);
    pthread_mutex_unlock(&ns->lock);

    if (count == 0) {
        hal_oled_string(0, 25, "No devices");
        hal_oled_string(0, 40, "ENTER:Scan");
    } else {
        if (selected_device >= count) selected_device = count-1;
        int y = 18;
        for (int i=0; i<4 && i+lan_scroll_offset < count; i++) {
            int idx = lan_scroll_offset + i;
            char line[64];
            snprintf(line, sizeof(line), "%s%d:%s", idx==selected_device ? "> " : " ", idx+1, devices[idx]);
            hal_oled_string(0, y, line);
            y +=10;
        }
    }
    
    hal_oled_refresh();
}

// ==================== 事件处理（删除所有配网按键逻辑）====================
void page_network_handle_event(event_t ev)
{
    const char *name = menu_current->name;

    // 网络主页面
    if (!strcmp(name, "Network"))
    {
        if (ev == EV_ENTER)
        {
            menu_current = list_node;
            list_selected = 0;
        }
        if (ev == EV_BACK)
        {
            extern void menu_back(void);
            menu_back();
        }
    }
    // 网络工具菜单（仅Status、LAN Scan）
    else if (!strcmp(name, "Network Tools"))
    {
        switch (ev)
        {
        case EV_UP:
            list_selected = (list_selected - 1 + 2) % 2; // 仅2个选项，循环切换
            break;
        case EV_DOWN:
            list_selected = (list_selected + 1) % 2;
            break;
        case EV_ENTER:
            menu_current = list_node->children[list_selected];
            break;
        case EV_BACK:
        {
            extern void menu_back(void);
            menu_back();
        }
        break;
        }
    }
    // 网络状态页面
    else if (!strcmp(name, "Status"))
    {
        if (ev == EV_UP || ev == EV_DOWN)
            status_page_index ^= 1; // 切换显示内容
        if (ev == EV_BACK)
        {
            extern void menu_back(void);
            menu_back();
        }
    }
    // LAN扫描页面
    else if (!strcmp(name, "LAN Scan"))
    {
        if (show_ping_result && ev == EV_BACK)
        {
            show_ping_result = 0;
            return;
        }

        network_state_t *ns = &g_network_state;
        int count = 0;
        pthread_mutex_lock(&ns->lock);
        count = ns->lan_device_count;
        pthread_mutex_unlock(&ns->lock);

        switch (ev)
        {
        case EV_UP:
            if (count > 0)
                selected_device = (selected_device - 1 + count) % count;
            break;
        case EV_DOWN:
            if (count > 0)
                selected_device = (selected_device + 1) % count;
            break;
        case EV_ENTER:
            if (count == 0)
                network_trigger_lan_scan(); // 扫描LAN设备
            else
            {
                // Ping选中的设备
                char ip[32] = {0};
                pthread_mutex_lock(&ns->lock);
                if (selected_device >= 0 && selected_device < count)
                    strncpy(ip, ns->lan_devices[selected_device], 31);
                pthread_mutex_unlock(&ns->lock);
                
                if (ip[0])
                {
                    int latency = ping_device(ip);
                    show_ping_result = 1;
                    snprintf(ping_result, sizeof(ping_result), "%s: %dms", ip, latency > 0 ? latency : -1);
                }
            }
            break;
        case EV_BACK:
        {
            extern void menu_back(void);
            menu_back();
        }
        break;
        }
    }
}

// ==================== 初始化（删除WiFi Config相关注册）====================
void page_network_init(void)
{
    static int init = 0;
    if (init) return;
    
    network_monitor_init();
    // 仅注册保留的页面
    page_register("Network", page_network_root_draw, page_network_handle_event);
    page_register("Network Tools", page_network_list_draw, page_network_handle_event);
    page_register("Status", page_network_status_draw, page_network_handle_event);
    page_register("LAN Scan", page_network_lan_draw, page_network_handle_event);
    
    init = 1;
}

// ==================== 创建菜单（删除WiFi Config子项）====================
menu_item_t *page_network_create_menu(void)
{
    page_network_init();
    if (!net_root)
    {
        extern const unsigned char icon_network_28x28[];
        net_root = menu_create(" Network", page_network_root_draw, icon_network_28x28);
        list_node = menu_create("Network Tools", page_network_list_draw, NULL);
        status_node = menu_create("Status", page_network_status_draw, NULL);
        lan_scan_node = menu_create("LAN Scan", page_network_lan_draw, NULL);

        // 仅添加Status、LAN Scan子项
        menu_add_child(net_root, list_node);
        menu_add_child(list_node, status_node);
        menu_add_child(list_node, lan_scan_node);
    }
    return net_root;
}