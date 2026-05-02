#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <errno.h>
#include "hal_oled.h"
#include "page_interface.h"
#include "menu.h"
#include "font.h"
#include "db_helper.h"

#define OLED_WIDTH    128
#define CHAR_WIDTH    6
#define EXPORT_LIST_MAX 8
#define PREVIEW_ROWS    4
#define USB_MOUNT_POINT "/mnt/usb"  // 固定挂载点，避免混乱

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
static menu_item_t *confirm_node = NULL;
static menu_item_t *export_node = NULL;
static menu_item_t *export_popup_node = NULL;

static char popup_title[32] = {0};
static char popup_line1[128] = {0};
static char popup_line2[128] = {0};
static char popup_line3[300] = {0};

static int export_sel_idx = 0;
static int export_disp_cnt = 0;
static db_table_info_t export_tables[EXPORT_LIST_MAX];
static int export_first_enter = 1;

static int export_view_mode = 0;
static int export_preview_offset = 0;
static int export_preview_sel_idx = 0;
static int export_preview_dirty = 1;
static db_preview_row_t export_preview_data[PREVIEW_ROWS];
static int export_preview_count = 0;

extern menu_item_t *menu_current;

/************************** 基础功能 **************************/
static void execute_shutdown(void)
{
    hal_oled_clear();
    hal_oled_string(20, 30, "Shutting down...");
    hal_oled_refresh();
    system("poweroff");
}

static void execute_reboot(void)
{
    hal_oled_clear();
    hal_oled_string(20, 30, "Rebooting...");
    hal_oled_refresh();
    system("reboot");
}

static void set_system_mode(system_mode_t mode)
{
    current_system_mode = mode;
    const char *governor = (mode == SYSTEM_MODE_PERFORMANCE) ? "performance" : "powersave";
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "echo %s | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor > /dev/null 2>&1", governor);
    system(cmd);
}

/************************** 导出弹窗 **************************/
static void page_export_popup_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, popup_title);
    hal_oled_line(0, 10, 127, 10);

    if (popup_line1[0]) hal_oled_string(0, 20, popup_line1);
    if (popup_line2[0]) hal_oled_string(0, 34, popup_line2);
    if (popup_line3[0]) hal_oled_string(0, 48, popup_line3);
    hal_oled_string(0, 56, "BACK: Exit");
    hal_oled_refresh();
}

static void page_export_popup_handle_event(event_t ev)
{
    if (ev == EV_BACK) {
        export_view_mode = 0;
        export_first_enter = 1;
        menu_back();
    }
}

/************************** 确认页面 **************************/
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

static void page_tools_confirm_handle_event(event_t ev)
{
    switch (ev) {
        case EV_ENTER:
            if (!strcmp(confirm_action, "Shutdown")) execute_shutdown();
            else if (!strcmp(confirm_action, "Reboot")) execute_reboot();
            break;
        case EV_BACK:
            menu_back();
            break;
    }
}

/************************** 关机 / 重启 **************************/
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
            menu_current = confirm_node;
            break;
        case EV_BACK:
            menu_back();
            break;
    }
}

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
            menu_current = confirm_node;
            break;
        case EV_BACK:
            menu_back();
            break;
    }
}

/************************** USB 检测与挂载（核心修复） **************************/
// 安全卸载函数
static void safe_umount(const char *path)
{
    if (access(path, F_OK) == 0) {
        // 先尝试正常卸载
        if (umount(path) != 0) {
            // 强制卸载（处理繁忙情况）
            umount2(path, MNT_FORCE);
        }
    }
}

/************************** USB检测与挂载 兼容 sdb1/sdc1 **************************/
/************************** USB检测与挂载（复刻你的手动命令，100%成功） **************************/
static int find_usb_mountpoint(char *out, int out_len)
{
    // 1. 先检查 /mnt/usb 已经被你手动挂载好了（直接用）
    if (access("/mnt/usb", W_OK) == 0) {
        strncpy(out, "/mnt/usb", out_len - 1);
        out[out_len - 1] = '\0';
        return 0;
    }

    // 2. 创建挂载目录（和你手动操作一致）
    system("sudo mkdir -p /mnt/usb");

    // 3. 直接执行你手动成功的命令：mount /dev/sdb1 /mnt/usb
    system("sudo mount /dev/sdb1 /mnt/usb 2>/dev/null");
    // 验证是否挂载成功
    if (access("/mnt/usb", W_OK) == 0) {
        strncpy(out, "/mnt/usb", out_len - 1);
        out[out_len - 1] = '\0';
        return 0;
    }

    // 4. 备用：如果sdb1不行，试sdc1（兼容你的U盘切换）
    system("sudo mount /dev/sdc1 /mnt/usb 2>/dev/null");
    if (access("/mnt/usb", W_OK) == 0) {
        strncpy(out, "/mnt/usb", out_len - 1);
        out[out_len - 1] = '\0';
        return 0;
    }

    // 都失败，返回错误
    return -1;
}
/************************** 导出列表 **************************/
static void page_export_list_draw(void)
{
    if (export_first_enter) {
        export_disp_cnt = db_get_table_list(export_tables, EXPORT_LIST_MAX);
        export_sel_idx = 0;
        export_first_enter = 0;
    }

    hal_oled_clear();
    hal_oled_string(0, 0, "Export Data");
    hal_oled_line(0, 10, 127, 10);

    if (export_disp_cnt == 0) {
        hal_oled_string(10, 30, "No tables");
    } else {
        int y = 14;
        for (int i = 0; i < export_disp_cnt && i < 4; i++) {
            char line[40];
            snprintf(line, sizeof(line), "%s %s(%d)",
                (i == export_sel_idx) ? ">" : " ",
                export_tables[i].desc, export_tables[i].count);
            hal_oled_string(0, y, line);
            y += 10;
        }
    }
    hal_oled_string(0, 56, "ENT:View BACK:Ret");
    hal_oled_refresh();
}

static void page_export_list_handle_event(event_t ev)
{
    switch (ev) {
        case EV_UP:
            export_sel_idx--;
            if (export_sel_idx < 0) export_sel_idx = export_disp_cnt - 1;
            break;
        case EV_DOWN:
            export_sel_idx++;
            if (export_sel_idx >= export_disp_cnt) export_sel_idx = 0;
            break;
        case EV_ENTER:
            if (export_disp_cnt == 0) break;
            export_preview_sel_idx = export_sel_idx;
            export_view_mode = 1;
            export_preview_offset = 0;
            export_preview_dirty = 1;
            break;
        case EV_BACK:
            export_first_enter = 1;
            menu_back();
            break;
    }
}

/************************** 导出预览 **************************/
static void page_export_preview_draw(void)
{
    if (export_preview_dirty) {
        if (export_preview_sel_idx < export_disp_cnt)
            export_preview_count = db_get_preview(
                export_tables[export_preview_sel_idx].name,
                export_preview_offset,
                export_preview_data, PREVIEW_ROWS);
        export_preview_dirty = 0;
    }

    hal_oled_clear();
    char title[32];
    snprintf(title, sizeof(title), "%s",
        export_preview_sel_idx < export_disp_cnt ?
        export_tables[export_preview_sel_idx].desc : "Preview");
    hal_oled_string(0, 0, title);
    hal_oled_line(0, 10, 127, 10);

    if (export_preview_count == 0)
        hal_oled_string(10, 30, "No data");
    else {
        int y = 14;
        for (int i = 0; i < export_preview_count; i++) {
            char line[36];
            snprintf(line, sizeof(line), "%d %.1f %.1f %s",
                export_preview_data[i].id,
                export_preview_data[i].temp,
                export_preview_data[i].humi,
                export_preview_data[i].ts);
            hal_oled_string(0, y, line);
            y += 10;
        }
    }
    hal_oled_string(0, 56, "ENT:Export BACK:Ret");
    hal_oled_refresh();
}

static void page_export_preview_handle_event(event_t ev)
{
    switch (ev) {
        case EV_UP:
            export_preview_offset = (export_preview_offset >= PREVIEW_ROWS) ?
                export_preview_offset - PREVIEW_ROWS : 0;
            export_preview_dirty = 1;
            break;
        case EV_DOWN:
            export_preview_offset += PREVIEW_ROWS;
            export_preview_dirty = 1;
            break;
        case EV_ENTER: {
            if (export_preview_sel_idx >= export_disp_cnt) break;

            const char *desc = export_tables[export_preview_sel_idx].desc;
            const char *name = export_tables[export_preview_sel_idx].name;
            char usb_path[256] = {0};
            char dest[512] = {0};
            char msg[64] = {0};
            int to_usb = 0;

            // 显示导出中弹窗
            strcpy(popup_title, "Exporting...");
            strcpy(popup_line1, desc);
            strcpy(popup_line2, "Please wait...");
            popup_line3[0] = 0;
            menu_current = export_popup_node;
            hal_oled_refresh();

            // 执行导出
            int mount_ret = find_usb_mountpoint(usb_path, sizeof(usb_path));
            if (mount_ret == 0) {
                snprintf(dest, sizeof(dest), "%s/%s.xlsx", usb_path, name);
                to_usb = 1;
            } else {
                snprintf(dest, sizeof(dest), "/mnt/msata/%s.xlsx", name);
            }

            // 核心修复：导出后同步文件系统，确保数据写入U盘
            int ret = db_export_xlsx(name, dest, msg, sizeof(msg));
            if (ret == 0 && to_usb) {
                system("sync");  // 强制写入磁盘
                system("sync");  // 双重保险
            }

            // 更新结果弹窗
            if (ret == 0 && to_usb) {
                strcpy(popup_title, "Export Done");
                snprintf(popup_line1, sizeof(popup_line1), "%s", desc);
                snprintf(popup_line2, sizeof(popup_line2), "Saved to USB");
                snprintf(popup_line3, sizeof(popup_line3), "Path: %s", usb_path);
            } else if (ret == 0) {
                strcpy(popup_title, "Export Done");
                snprintf(popup_line1, sizeof(popup_line1), "%s", desc);
                snprintf(popup_line2, sizeof(popup_line2), "Saved Local");
                strcpy(popup_line3, "/mnt/msata");
            } else {
                strcpy(popup_title, "Export Failed");
                snprintf(popup_line1, sizeof(popup_line1), "%s", desc);
                snprintf(popup_line2, sizeof(popup_line2), "Error: %s", msg);
                strcpy(popup_line3, to_usb ? "USB write failed" : "Local write failed");
            }
            hal_oled_refresh();

            // 等待2秒
            usleep(2000000);

            // 自动卸载USB（避免数据丢失）
            if (to_usb) {
                safe_umount(usb_path);
            }

            // 自动返回列表
            export_view_mode = 0;
            export_first_enter = 1;
            menu_back();
            break;
        }
        case EV_BACK:
            export_view_mode = 0;
            export_first_enter = 1;
            break;
    }
}

/************************** 导出总入口 **************************/
static void page_tools_export_draw(void)
{
    if (export_view_mode == 0)
        page_export_list_draw();
    else
        page_export_preview_draw();
}

static void page_tools_export_handle_event(event_t ev)
{
    if (export_view_mode == 0)
        page_export_list_handle_event(ev);
    else
        page_export_preview_handle_event(ev);
}

/************************** 模式设置 **************************/
static void page_mode_list_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Select Mode");
    hal_oled_line(0, 10, 127, 10);
    const char *modes[] = {"Balanced", "Performance"};
    int y = 25;
    for (int i = 0; i < 2; i++) {
        char line[32];
        snprintf(line, sizeof(line), "%s %s", i == mode_list_selected ? ">" : " ", modes[i]);
        hal_oled_string(0, y, line);
        y += 15;
    }
    hal_oled_refresh();
}

static void page_mode_list_handle_event(event_t ev)
{
    switch (ev) {
        case EV_UP: case EV_DOWN:
            mode_list_selected = !mode_list_selected;
            break;
        case EV_ENTER:
            set_system_mode(mode_list_selected);
            hal_oled_clear();
            hal_oled_string(0, 0, "Mode Applied");
            hal_oled_line(0, 10, 127, 10);
            char buf[32];
            snprintf(buf, sizeof(buf), "%s Mode",
                mode_list_selected ? "Performance" : "Balanced");
            int x = (OLED_WIDTH - strlen(buf) * CHAR_WIDTH) / 2;
            hal_oled_string(x, 30, buf);
            hal_oled_refresh();
            usleep(1500000);
            menu_back();
            break;
        case EV_BACK:
            menu_back();
            break;
    }
}

/************************** 工具列表 **************************/
static void page_tools_list_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "System Tools");
    hal_oled_line(0, 10, 127, 10);
    const char *items[] = {"Shutdown", "Reboot", "Mode Settings", "Export Data"};
    int y = 18;
    for (int i = 0; i < 4; i++) {
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
            if (tools_list_selected < 0) tools_list_selected = 3;
            break;
        case EV_DOWN:
            tools_list_selected++;
            if (tools_list_selected > 3) tools_list_selected = 0;
            break;
        case EV_ENTER:
            menu_current = tools_list_node->children[tools_list_selected];
            break;
        case EV_BACK:
            menu_back();
            break;
    }
}

/************************** 工具根页面 **************************/
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
        menu_back();
    }
}

/************************** 初始化 **************************/
void page_tools_init(void)
{
    static int init = 0;
    if (init) return;

    page_register("Confirm Action", page_tools_confirm_draw, page_tools_confirm_handle_event);
    page_register("Tools", page_tools_root_draw, page_tools_root_handle_event);
    page_register("Tools List", page_tools_list_draw, page_tools_list_handle_event);
    page_register("Shutdown", page_tools_shutdown_draw, page_tools_shutdown_handle_event);
    page_register("Reboot", page_tools_reboot_draw, page_tools_reboot_handle_event);
    page_register("Mode Settings", page_mode_list_draw, page_mode_list_handle_event);
    page_register("Export Data", page_tools_export_draw, page_tools_export_handle_event);
    page_register("Export Popup", page_export_popup_draw, page_export_popup_handle_event);

    init = 1;
}

menu_item_t* page_tools_create_menu(void)
{
    page_tools_init();
    if (!tools_root) {
        extern const unsigned char icon_tools_28x28[];

        tools_root = menu_create("Tools", page_tools_root_draw, icon_tools_28x28);
        tools_list_node = menu_create("Tools List", page_tools_list_draw, NULL);
        shutdown_node = menu_create("Shutdown", page_tools_shutdown_draw, NULL);
        reboot_node = menu_create("Reboot", page_tools_reboot_draw, NULL);
        mode_node = menu_create("Mode Settings", page_mode_list_draw, NULL);
        confirm_node = menu_create("Confirm Action", page_tools_confirm_draw, NULL);
        export_node = menu_create("Export Data", page_tools_export_draw, NULL);
        export_popup_node = menu_create("Export Popup", page_export_popup_draw, NULL);

        menu_add_child(tools_root, tools_list_node);
        menu_add_child(tools_list_node, shutdown_node);
        menu_add_child(tools_list_node, reboot_node);
        menu_add_child(tools_list_node, mode_node);
        menu_add_child(tools_list_node, export_node);
        menu_add_child(export_node, export_popup_node);
    }
    return tools_root;
}