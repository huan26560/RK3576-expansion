#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include "hal_oled.h"
#include "page_interface.h"
#include "menu.h"
#include "font.h"

#define OLED_WIDTH    128
#define CHAR_WIDTH    6

typedef enum {
    SYSTEM_MODE_BALANCED = 0,
    SYSTEM_MODE_PERFORMANCE = 1
} system_mode_t;

static system_mode_t current_system_mode = SYSTEM_MODE_BALANCED;
static char confirm_action[32] = {0};

static int tools_list_selected = 0;
static int mode_list_selected = 0;

static menu_item_t *tools_root = NULL;
static menu_item_t *tools_list_node = NULL;
static menu_item_t *shutdown_node = NULL;
static menu_item_t *reboot_node = NULL;
static menu_item_t *mode_node = NULL;
// 🔧 1. 提前定义确认页节点（全局）
static menu_item_t *confirm_node = NULL;

extern menu_item_t *menu_current;

static void execute_shutdown(void)
{
    hal_oled_clear();
    hal_oled_string(20, 30, "Shutting down...");
    hal_oled_refresh();
    usleep(1000000);
    system("poweroff");
}

static void execute_reboot(void)
{
    hal_oled_clear();
    hal_oled_string(20, 30, "Rebooting...");
    hal_oled_refresh();
    usleep(1000000);
    system("reboot");
}

static void set_system_mode(system_mode_t mode)
{
    current_system_mode = mode;
    const char *governor = (mode == SYSTEM_MODE_PERFORMANCE) ? "performance" : "powersave";
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "echo %s | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor > /dev/null 2>&1", governor);
    system(cmd);
    printf("[Tools] Set mode: %s\n", (mode == SYSTEM_MODE_PERFORMANCE) ? "Performance" : "Balanced");
}

// 确认页面绘制
static void page_tools_confirm_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Confirm Action");
    hal_oled_line(0, 10, 127, 10);
    
    char buf[64];
    snprintf(buf, sizeof(buf), "Execute %s?", confirm_action);
    hal_oled_string(10, 30, buf);
    hal_oled_refresh();
}

// 确认页面事件处理
static void page_tools_confirm_handle_event(event_t ev)
{
    switch (ev) {
        case EV_ENTER:
            if (strcmp(confirm_action, "Shutdown") == 0) {
                execute_shutdown();
            } else if (strcmp(confirm_action, "Reboot") == 0) {
                execute_reboot();
            }
            break;
        case EV_BACK:
            extern void menu_back(void);
            menu_back();
            break;
    }
}

// 关机页面
static void page_tools_shutdown_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Shutdown System");
    hal_oled_line(0, 10, 127, 10);
    hal_oled_string(10, 30, "Power off device?");
    hal_oled_refresh();
}

static void page_tools_shutdown_handle_event(event_t ev)
{
    switch (ev) {
        case EV_ENTER:
            strcpy(confirm_action, "Shutdown");
            // 🔧 2. 直接切换到提前创建好的确认页节点（不再调用menu_create）
            menu_current = confirm_node;
            break;
        case EV_BACK:
            extern void menu_back(void);
            menu_back();
            break;
    }
}

// 重启页面
static void page_tools_reboot_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Reboot System");
    hal_oled_line(0, 10, 127, 10);
    hal_oled_string(10, 30, "Restart device?");
    hal_oled_refresh();
}

static void page_tools_reboot_handle_event(event_t ev)
{
    switch (ev) {
        case EV_ENTER:
            strcpy(confirm_action, "Reboot");
            // 🔧 3. 同样切换到确认页节点
            menu_current = confirm_node;
            break;
        case EV_BACK:
            extern void menu_back(void);
            menu_back();
            break;
    }
}

// 模式选择页面（无修改）
static void page_tools_mode_list_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Select Mode");
    hal_oled_line(0, 10, 127, 10);
    
    const char *modes[] = {"Balanced", "Performance"};
    int count = 2;
    
    int y = 25;
    for (int i = 0; i < count; i++) {
        char line[32];
        snprintf(line, sizeof(line), "%s %s", i == mode_list_selected ? ">" : " ", modes[i]);
        hal_oled_string(0, y, line);
        y += 15;
    }
    
    hal_oled_refresh();
}

static void page_tools_mode_list_handle_event(event_t ev)
{
    switch (ev) {
        case EV_UP:
            mode_list_selected--;
            if (mode_list_selected < 0) mode_list_selected = 1;
            break;
        case EV_DOWN:
            mode_list_selected++;
            if (mode_list_selected > 1) mode_list_selected = 0;
            break;
        case EV_ENTER:
            set_system_mode(mode_list_selected);
            hal_oled_clear();
            hal_oled_string(0, 0, "Mode Applied");
            hal_oled_line(0, 10, 127, 10);
            char buf[32];
            snprintf(buf, sizeof(buf), "%s Mode", mode_list_selected == SYSTEM_MODE_PERFORMANCE ? "Performance" : "Balanced");
            int x = (OLED_WIDTH - strlen(buf) * CHAR_WIDTH) / 2;
            hal_oled_string(x, 30, buf);
            hal_oled_string(0, 55, "BACK: Return");
            hal_oled_refresh();
            usleep(2000000);
            extern void menu_back(void);
            menu_back();
            break;
        case EV_BACK:
            extern void menu_back(void);
            menu_back();
            break;
    }
}

// 工具列表页面（无修改）
static void page_tools_list_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "System Tools");
    hal_oled_line(0, 10, 127, 10);
    
    const char *items[] = {"Shutdown", "Reboot", "Mode Settings"};
    int count = 3;
    
    int y = 18;
    for (int i = 0; i < count; i++) {
        char line[32];
        snprintf(line, sizeof(line), "%s %s", i == tools_list_selected ? ">" : " ", items[i]);
        hal_oled_string(0, y, line);
        y += 10;
    }
    hal_oled_refresh();
}

static void page_tools_list_handle_event(event_t ev)
{
    switch (ev) {
        case EV_UP:
            tools_list_selected--;
            if (tools_list_selected < 0) tools_list_selected = 2;
            break;
        case EV_DOWN:
            tools_list_selected++;
            if (tools_list_selected > 2) tools_list_selected = 0;
            break;
        case EV_ENTER:
            menu_current = tools_list_node->children[tools_list_selected];
            break;
        case EV_BACK:
            extern void menu_back(void);
            menu_back();
            break;
    }
}

// 工具根页面（无修改）
static void page_tools_root_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "System Tools");
    hal_oled_line(0, 10, 127, 10);
    
    char mode_str[32];
    snprintf(mode_str, sizeof(mode_str), "Mode: %s", 
             current_system_mode == SYSTEM_MODE_PERFORMANCE ? "Performance" : "Balanced");
    hal_oled_string(0, 25, mode_str);
    
    hal_oled_string(0, 45, "Press ENTER for tools");
    hal_oled_refresh();
}

static void page_tools_root_handle_event(event_t ev)
{
    if (ev == EV_ENTER) {
        menu_current = tools_list_node;
        tools_list_selected = 0;
    } else if (ev == EV_BACK) {
        extern void menu_back(void);
        menu_back();
    }
}

// 初始化（注册确认页）
void page_tools_init(void)
{
    static int initialized = 0;
    if (initialized) return;
    
    // 注册确认页
    page_register("Confirm Action", page_tools_confirm_draw, page_tools_confirm_handle_event);
    page_register("Tools", page_tools_root_draw, page_tools_root_handle_event);
    page_register("Tools List", page_tools_list_draw, page_tools_list_handle_event);
    page_register("Shutdown", page_tools_shutdown_draw, page_tools_shutdown_handle_event);
    page_register("Reboot", page_tools_reboot_draw, page_tools_reboot_handle_event);
    page_register("Mode Settings", page_tools_mode_list_draw, page_tools_mode_list_handle_event);
    
    initialized = 1;
}

// 创建菜单树（创建确认页节点）
menu_item_t* page_tools_create_menu(void)
{
    page_tools_init();
    
    if (tools_root == NULL) {
        extern const unsigned char icon_tools_28x28[];
        
        tools_root = menu_create("Tools", page_tools_root_draw, icon_tools_28x28);
        tools_list_node = menu_create("Tools List", page_tools_list_draw, NULL);
        
        shutdown_node = menu_create("Shutdown", page_tools_shutdown_draw, NULL);
        reboot_node = menu_create("Reboot", page_tools_reboot_draw, NULL);
        mode_node = menu_create("Mode Settings", page_tools_mode_list_draw, NULL);
        // 🔧 4. 创建确认页节点（第三个参数传NULL，因为无图标）
        confirm_node = menu_create("Confirm Action", page_tools_confirm_draw, NULL);
        
        // 添加子节点（确认页无需添加到树，仅作为临时切换页）
        menu_add_child(tools_root, tools_list_node);
        menu_add_child(tools_list_node, shutdown_node);
        menu_add_child(tools_list_node, reboot_node);
        menu_add_child(tools_list_node, mode_node);
    }
    
    return tools_root;
}