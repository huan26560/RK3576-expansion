#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "hal_oled.h"
#include "menu.h"
#include "font.h"
#include "system_monitor.h"
#include "hal_system.h"

#define LINE_HEIGHT 12

// UI状态
static int current_page = 0;
static int selected_item = 0;
static int scroll_offset = 0;  // 滚动偏移

// 菜单节点
static menu_item_t *system_root = NULL;
static menu_item_t *tools_node = NULL;      // ✅ 新增：System Tools列表页
static menu_item_t *summary_node = NULL;    // System汇总页（作为独立页面）
static menu_item_t *detail_node = NULL;
static menu_item_t *gpu_node = NULL;
static menu_item_t *npu_node = NULL;
static menu_item_t *memory_node = NULL;

extern menu_item_t *menu_current;

// 页面枚举
typedef enum {
    PAGE_CPU_SUMMARY = 0,  // 在System Tools列表中的索引
    PAGE_CPU_DETAIL,
    PAGE_GPU_INFO,
    PAGE_NPU_INFO,
    PAGE_MEMORY,
    PAGE_COUNT
} system_page_t;

// ✅ 新增：System Tools列表页绘制
static void page_system_tools_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "System Tools");
    hal_oled_line(0, 10, 127, 10);
    
    const char *items[] = {"CPU Detail", "GPU Info", "NPU Info", "Memory"};
    int y = 18;
    
    for (int i = 0; i < PAGE_COUNT - 1; i++) {  // 排除PAGE_CPU_SUMMARY
        char line[32];
        snprintf(line, sizeof(line), "%s %s", 
                 i == selected_item ? ">" : " ", 
                 items[i]);
        hal_oled_string(0, y, line);
        y += LINE_HEIGHT;
    }
    
    hal_oled_refresh();
}

// System汇总页（仅显示，不导航）
static void page_system_summary_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "System");
    hal_oled_line(0, 10, 127, 10);
    
    system_state_t state;
    system_monitor_get_state(&state);
    
    char buf[64];
    int y = 18;
    
    snprintf(buf, sizeof(buf), "CPU Total: %d%%", state.cpu_total_usage);
    hal_oled_string(0, y, buf);
    y += LINE_HEIGHT;
    
    // 大小核平均
    int little_freq = 0, big_freq = 0, little_cnt = 0, big_cnt = 0;
    for (int i = 0; i < state.cpu_core_count; i++) {
        if (state.cpu_cores[i].type == CORE_LITTLE) {
            little_freq += state.cpu_cores[i].freq_mhz;
            little_cnt++;
        } else {
            big_freq += state.cpu_cores[i].freq_mhz;
            big_cnt++;
        }
    }
    
    if (little_cnt > 0) {
        snprintf(buf, sizeof(buf), "LITTLE: %dMHz avg", little_freq / little_cnt);
        hal_oled_string(0, y, buf);
        y += LINE_HEIGHT;
    }
    
    if (big_cnt > 0) {
        snprintf(buf, sizeof(buf), "BIG: %dMHz avg", big_freq / big_cnt);
        hal_oled_string(0, y, buf);
    }
    
    hal_oled_refresh();
}

// CPU详情（保留滚动逻辑）
static void page_system_detail_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "CPU Detail");
    hal_oled_line(0, 10, 127, 10);
    
    system_state_t state;
    system_monitor_get_state(&state);
    
    const int visible_lines = 3;
    const int line_height = LINE_HEIGHT;
    
    if (selected_item < scroll_offset) {
        scroll_offset = selected_item;
    } else if (selected_item >= scroll_offset + visible_lines) {
        scroll_offset = selected_item - visible_lines + 1;
    }
    
    if (scroll_offset < 0) scroll_offset = 0;
    if (scroll_offset > state.cpu_core_count - visible_lines) {
        scroll_offset = state.cpu_core_count - visible_lines;
    }
    
    int y = 18;
    for (int i = scroll_offset; i < scroll_offset + visible_lines && i < state.cpu_core_count; i++) {
        core_info_t *core = &state.cpu_cores[i];
        char buf[64];
        
        snprintf(buf, sizeof(buf), "%s%s %dMHz %d%%", 
                 core->name,
                 core->type == CORE_LITTLE ? "(L)" : "(B)",
                 core->freq_mhz,
                 core->usage_percent);
        
        char display_buf[64];
        snprintf(display_buf, sizeof(display_buf), "%s%s", 
                 (i == selected_item) ? ">" : " ", buf);
        
        hal_oled_string(0, y, display_buf);
        y += line_height;
    }
    
    if (state.cpu_core_count > visible_lines) {
        char hint[16];
        snprintf(hint, sizeof(hint), "%d/%d", selected_item + 1, state.cpu_core_count);
        hal_oled_string(100, 55, hint);
    }
    
    hal_oled_refresh();
}

// GPU绘制
static void page_system_gpu_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "GPU Info");
    hal_oled_line(0, 10, 127, 10);
    
    system_state_t state;
    system_monitor_get_state(&state);
    
    char buf[64];
    int y = 20;
    
    snprintf(buf, sizeof(buf), "Freq: %d MHz", state.gpu.freq_mhz);
    hal_oled_string(0, y, buf);
    y += LINE_HEIGHT;
    
    snprintf(buf, sizeof(buf), "Usage: %d%%", state.gpu.usage_percent);
    hal_oled_string(0, y, buf);
    y += LINE_HEIGHT;
    
    snprintf(buf, sizeof(buf), "Temp: %d°C", state.gpu.temp_c);
    hal_oled_string(0, y, buf);
    
    hal_oled_refresh();
}

// NPU绘制
// NPU绘制（支持多核心显示）
// NPU绘制（显示多核心使用率）
static void page_system_npu_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "NPU Info");
    hal_oled_line(0, 10, 127, 10);
    
    system_state_t state;
    system_monitor_get_state(&state);
    
    char buf[64];
    int y = 20;
    
    // 显示频率
    snprintf(buf, sizeof(buf), "Freq: %d MHz", state.npu.freq_mhz);
    hal_oled_string(0, y, buf);
    y += LINE_HEIGHT;
    
    // 显示Core0使用率
    snprintf(buf, sizeof(buf), "Core0: %d%%", state.npu.core_usage[0]);
    hal_oled_string(0, y, buf);
    y += LINE_HEIGHT;
    
    // 显示Core1使用率
    snprintf(buf, sizeof(buf), "Core1: %d%%", state.npu.core_usage[1]);
    hal_oled_string(0, y, buf);
    y += LINE_HEIGHT;
    
    // 显示总使用率（平均值）
    snprintf(buf, sizeof(buf), "Total: %d%%", state.npu.usage_percent);
    hal_oled_string(0, y, buf);
    y += LINE_HEIGHT;
    
    // 显示温度
    snprintf(buf, sizeof(buf), "Temp: %d°C", state.npu.temp_c);
    hal_oled_string(0, y, buf);
    
    hal_oled_refresh();
}
// Memory绘制
static void page_system_memory_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Memory");
    hal_oled_line(0, 10, 127, 10);
    
    system_state_t state;
    system_monitor_get_state(&state);
    
    char buf[64];
    int y = 18;
    
    snprintf(buf, sizeof(buf), "Total: %dMB", state.memory.total_mb);
    hal_oled_string(0, y, buf);
    y += LINE_HEIGHT;
    
    snprintf(buf, sizeof(buf), "Used: %d%%", state.memory.used_percent);
    hal_oled_string(0, y, buf);
    y += LINE_HEIGHT;
    
    snprintf(buf, sizeof(buf), "Avail: %dMB", state.memory.available_mb);
    hal_oled_string(0, y, buf);
    y += LINE_HEIGHT;
    
    snprintf(buf, sizeof(buf), "Free: %dMB", state.memory.free_mb);
    hal_oled_string(0, y, buf);
    y += LINE_HEIGHT;
    
    snprintf(buf, sizeof(buf), "Buff: %dMB", state.memory.buffers_mb);
    hal_oled_string(0, y, buf);
    
    hal_oled_refresh();
}

// 事件处理
void page_system_handle_event(event_t ev)
{
    const char *name = menu_current->name;
    
    if (strcmp(name, "System") == 0)  // ✅ 汇总页
    {
        if (ev == EV_ENTER) {
            menu_current = tools_node;  // ✅ 进入Tools列表
            selected_item = 0;
            scroll_offset = 0;
        } else if (ev == EV_BACK) {
            extern void menu_back(void);
            menu_back();
        }
    }
    else if (strcmp(name, "System Tools") == 0)  // ✅ Tools列表页
    {
        switch (ev) {
        case EV_UP:
            selected_item--;
            if (selected_item < 0) selected_item = PAGE_COUNT - 2;  // 排除汇总页
            break;
        case EV_DOWN:
            selected_item++;
            if (selected_item > PAGE_COUNT - 2) selected_item = 0;
            break;
        case EV_ENTER:  // ✅ 进入对应页面
            switch (selected_item) {
            case 0: menu_current = detail_node; break;
            case 1: menu_current = gpu_node; break;
            case 2: menu_current = npu_node; break;
            case 3: menu_current = memory_node; break;
            }
            break;
        case EV_BACK:
            selected_item = 0;
            extern void menu_back(void);
            menu_back();
            break;
        }
    }
    else if (strcmp(name, "CPU Detail") == 0)
    {
        system_state_t state;
        system_monitor_get_state(&state);
        
        switch (ev) {
        case EV_UP:
            selected_item--;
            if (selected_item < 0) selected_item = state.cpu_core_count - 1;
            break;
        case EV_DOWN:
            selected_item++;
            if (selected_item >= state.cpu_core_count) selected_item = 0;
            break;
        case EV_BACK:
            selected_item = 0;
            scroll_offset = 0;
            extern void menu_back(void);
            menu_back();
            break;
        }
    }
    else if (strcmp(name, "GPU Info") == 0 || 
             strcmp(name, "NPU Info") == 0 || 
             strcmp(name, "Memory") == 0)
    {
        if (ev == EV_BACK) {
            extern void menu_back(void);
            menu_back();
        }
    }
}

// 初始化
void page_system_init(void)
{
    static int initialized = 0;
    if (initialized) return;
    
    system_monitor_init();
    
    // ✅ 注册所有页面
    page_register("System", page_system_summary_draw, page_system_handle_event);
    page_register("System Tools", page_system_tools_draw, page_system_handle_event);  // ✅ 新增
    page_register("CPU Detail", page_system_detail_draw, page_system_handle_event);
    page_register("GPU Info", page_system_gpu_draw, page_system_handle_event);
    page_register("NPU Info", page_system_npu_draw, page_system_handle_event);
    page_register("Memory", page_system_memory_draw, page_system_handle_event);
    
    initialized = 1;
    printf("[UI] System pages registered\n");
}

// 创建菜单树
menu_item_t *page_system_create_menu(void)
{
    page_system_init();
    
    if (system_root == NULL)
    {
        // ✅ 创建层级菜单
        system_root = menu_create("System", page_system_summary_draw, icon_system_28x28);
        tools_node = menu_create("System Tools", page_system_tools_draw, NULL);  // ✅ 新增
        summary_node = system_root;  // 汇总页即根
        
        // 功能页
        detail_node = menu_create("CPU Detail", page_system_detail_draw, NULL);
        gpu_node = menu_create("GPU Info", page_system_gpu_draw, NULL);
        npu_node = menu_create("NPU Info", page_system_npu_draw, NULL);
        memory_node = menu_create("Memory", page_system_memory_draw, NULL);
        
        // ✅ 构建树：System → Tools → [Detail, GPU, NPU, Memory]
        menu_add_child(system_root, tools_node);
        menu_add_child(tools_node, detail_node);
        menu_add_child(tools_node, gpu_node);
        menu_add_child(tools_node, npu_node);
        menu_add_child(tools_node, memory_node);
        
        printf("[UI] System menu tree created\n");
    }
    
    return system_root;
}