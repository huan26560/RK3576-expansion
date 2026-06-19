#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <errno.h>
#include <math.h>
#include "hal_oled.h"
#include "page_interface.h"
#include "menu.h"
#include "font.h"
#include "db_helper.h"
#include "mqtt_client.h"

#define OLED_WIDTH 128
#define CHAR_WIDTH 6
#define EXPORT_LIST_MAX 8
#define PREVIEW_ROWS 4
#define USB_MOUNT_POINT "/mnt/usb"
#define CONTROL_TOPIC "device/control"

typedef enum
{
    SYSTEM_MODE_BALANCED = 0,
    SYSTEM_MODE_PERFORMANCE = 1
} system_mode_t;

static system_mode_t current_system_mode = SYSTEM_MODE_BALANCED;
static char confirm_action[32] = {0};

static int tools_list_selected = 0; // 当前选中的索引 (0~5)
static int tools_list_offset = 0;   // 当前页面第一个显示的索引
static int mode_list_selected = 0;

static menu_item_t *tools_list_node = NULL;
static menu_item_t *shutdown_node = NULL;
static menu_item_t *reboot_node = NULL;
static menu_item_t *restart_ui_node = NULL;
static menu_item_t *mode_node = NULL;
static menu_item_t *confirm_node = NULL;
static menu_item_t *export_node = NULL;
static menu_item_t *export_popup_node = NULL;
static menu_item_t *analysis_node = NULL;

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

typedef struct
{
    int count;
    double temp_mean, temp_std, temp_min, temp_max;
    double humi_mean, humi_std, humi_min, humi_max;
    double corr_pearson;
    double thi_mean;
    int temp_anom_3sigma, humi_anom_3sigma;
    double temp_trend_per_day;
    double humi_trend_per_day; // 新增
    char comfort_grade[16];
    char comfort_suggestion[32];
} analysis_result_t;

static int analysis_page = 0;        // 当前页码 (0-based)
static int analysis_total_pages = 4; // 固定4页
static analysis_result_t analysis_res = {0};
static int analysis_ready = 0;             // 0未分析，1成功，-1失败
static char analysis_display_lines[4][24]; // 每页最多4行，每行最多20字符

extern menu_item_t *menu_current;

// ====================== MQTT 广播 ======================
static void mqtt_broadcast_command(const char *cmd)
{
    if (mqtt_publish(CONTROL_TOPIC, cmd) == 0)
    {
        printf("[MQTT] Broadcasted command: %s\n", cmd);
    }
    else
    {
        printf("[MQTT] Broadcast failed for command: %s\n", cmd);
        hal_oled_string(0, 50, "MQTT send failed");
        hal_oled_refresh();
        sleep(1);
    }
}

// ====================== 基础功能 ======================
static void execute_shutdown(void)
{
    mqtt_broadcast_command("shutdown");
    sleep(1);
    hal_oled_clear();
    hal_oled_string(20, 30, "Shutting down...");
    hal_oled_refresh();
    system("poweroff");
}

static void execute_reboot(void)
{
    mqtt_broadcast_command("reboot");
    sleep(1);
    hal_oled_clear();
    hal_oled_string(20, 30, "Rebooting...");
    hal_oled_refresh();
    system("reboot");
}

static void execute_restart_ui(void)
{
    hal_oled_clear();
    hal_oled_string(20, 30, "Restarting UI...");
    hal_oled_refresh();
    system("sudo systemctl restart expansion-ui");
}

// ====================== 模式设置 ======================
static void set_devfreq_governor(const char *dev_name, const char *governor)
{
    char path[300];
    snprintf(path, sizeof(path), "/sys/class/devfreq/%s/governor", dev_name);
    if (access(path, W_OK) == 0)
    {
        FILE *fp = fopen(path, "w");
        if (fp)
        {
            fprintf(fp, "%s", governor);
            fclose(fp);
        }
        return;
    }
    DIR *dir = opendir("/sys/class/devfreq");
    if (!dir)
        return;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strstr(entry->d_name, dev_name) != NULL)
        {
            snprintf(path, sizeof(path), "/sys/class/devfreq/%s/governor", entry->d_name);
            FILE *fp = fopen(path, "w");
            if (fp)
            {
                fprintf(fp, "%s", governor);
                fclose(fp);
            }
            break;
        }
    }
    closedir(dir);
}

static void set_system_mode(system_mode_t mode)
{
    current_system_mode = mode;
    const char *governor = (mode == SYSTEM_MODE_PERFORMANCE) ? "performance" : "powersave";

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "echo %s | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor > /dev/null 2>&1", governor);
    system(cmd);

    set_devfreq_governor("gpu", governor);
    set_devfreq_governor("mali", governor);
    set_devfreq_governor("npu", governor);
    set_devfreq_governor("vpu", governor);
    set_devfreq_governor("dmc", governor);
    set_devfreq_governor("ddr", governor);
    set_devfreq_governor("dram", governor);

    const char *alt_paths[] = {
        "/sys/class/misc/mali/device/devfreq/ff9a0000.gpu/governor",
        "/sys/kernel/gpu/governor",
        "/sys/class/misc/mali/governor",
        NULL};
    if (mode == SYSTEM_MODE_PERFORMANCE)
    {
        for (int i = 0; alt_paths[i]; i++)
        {
            if (access(alt_paths[i], W_OK) == 0)
            {
                FILE *fp = fopen(alt_paths[i], "w");
                if (fp)
                {
                    fprintf(fp, "performance");
                    fclose(fp);
                }
            }
        }
    }
}

// ====================== 导出弹窗 ======================
static void page_export_popup_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, popup_title);
    hal_oled_line(0, 10, 127, 10);
    if (popup_line1[0])
        hal_oled_string(0, 20, popup_line1);
    if (popup_line2[0])
        hal_oled_string(0, 34, popup_line2);
    if (popup_line3[0])
        hal_oled_string(0, 48, popup_line3);
    hal_oled_string(0, 56, "BACK: Exit");
    hal_oled_refresh();
}

static void page_export_popup_handle_event(event_t ev)
{
    if (ev == EV_BACK)
    {
        export_view_mode = 0;
        export_first_enter = 1;
        menu_back();
    }
}

// ====================== 确认页面 ======================
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
    switch (ev)
    {
    case EV_ENTER:
        if (!strcmp(confirm_action, "Shutdown"))
            execute_shutdown();
        else if (!strcmp(confirm_action, "Reboot"))
            execute_reboot();
        else if (!strcmp(confirm_action, "Restart UI"))
            execute_restart_ui();
        break;
    case EV_BACK:
        menu_back();
        break;
    }
}

// ====================== 关机页面 ======================
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
    switch (ev)
    {
    case EV_ENTER:
        strcpy(confirm_action, "Shutdown");
        menu_current = confirm_node;
        break;
    case EV_BACK:
        menu_back();
        break;
    }
}

// ====================== 重启页面 ======================
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
    switch (ev)
    {
    case EV_ENTER:
        strcpy(confirm_action, "Reboot");
        menu_current = confirm_node;
        break;
    case EV_BACK:
        menu_back();
        break;
    }
}

// ====================== 重启UI页面 ======================
static void page_restart_ui_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Restart UI");
    hal_oled_line(0, 10, 127, 10);
    hal_oled_string(10, 30, "Restart UI service?");
    hal_oled_refresh();
}

static void page_restart_ui_handle_event(event_t ev)
{
    switch (ev)
    {
    case EV_ENTER:
        strcpy(confirm_action, "Restart UI");
        menu_current = confirm_node;
        break;
    case EV_BACK:
        menu_back();
        break;
    }
}

// ====================== USB 检测与挂载 ======================
static void safe_umount(const char *path)
{
    if (access(path, F_OK) == 0)
    {
        if (umount(path) != 0)
        {
            umount2(path, MNT_FORCE);
        }
    }
}

static int find_usb_mountpoint(char *out, int out_len)
{
    if (access("/mnt/usb", W_OK) == 0)
    {
        strncpy(out, "/mnt/usb", out_len - 1);
        out[out_len - 1] = '\0';
        return 0;
    }
    system("sudo mkdir -p /mnt/usb");
    system("sudo mount /dev/sdb1 /mnt/usb 2>/dev/null");
    if (access("/mnt/usb", W_OK) == 0)
    {
        strncpy(out, "/mnt/usb", out_len - 1);
        out[out_len - 1] = '\0';
        return 0;
    }
    system("sudo mount /dev/sdc1 /mnt/usb 2>/dev/null");
    if (access("/mnt/usb", W_OK) == 0)
    {
        strncpy(out, "/mnt/usb", out_len - 1);
        out[out_len - 1] = '\0';
        return 0;
    }
    return -1;
}

// ====================== 导出列表 ======================
static void page_export_list_draw(void)
{
    if (export_first_enter)
    {
        export_disp_cnt = db_get_table_list(export_tables, EXPORT_LIST_MAX);
        export_sel_idx = 0;
        export_first_enter = 0;
    }

    hal_oled_clear();
    hal_oled_string(0, 0, "Export Data");
    hal_oled_line(0, 10, 127, 10);

    if (export_disp_cnt == 0)
    {
        hal_oled_string(10, 30, "No tables");
    }
    else
    {
        int y = 14;
        for (int i = 0; i < export_disp_cnt && i < 4; i++)
        {
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
    switch (ev)
    {
    case EV_UP:
        export_sel_idx--;
        if (export_sel_idx < 0)
            export_sel_idx = export_disp_cnt - 1;
        break;
    case EV_DOWN:
        export_sel_idx++;
        if (export_sel_idx >= export_disp_cnt)
            export_sel_idx = 0;
        break;
    case EV_ENTER:
        if (export_disp_cnt == 0)
            break;
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

// ====================== 导出预览（已修改：不显示序号） ======================
static void page_export_preview_draw(void)
{
    if (export_preview_dirty)
    {
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
             export_preview_sel_idx < export_disp_cnt ? export_tables[export_preview_sel_idx].desc : "Preview");
    hal_oled_string(0, 0, title);
    hal_oled_line(0, 10, 127, 10);

    if (export_preview_count == 0)
        hal_oled_string(10, 30, "No data");
    else
    {
        int y = 14;
        for (int i = 0; i < export_preview_count; i++)
        {
            char line[36];
            snprintf(line, sizeof(line), "%.1f %.1f %s",
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
    switch (ev)
    {
    case EV_UP:
        export_preview_offset = (export_preview_offset >= PREVIEW_ROWS) ? export_preview_offset - PREVIEW_ROWS : 0;
        export_preview_dirty = 1;
        break;
    case EV_DOWN:
        export_preview_offset += PREVIEW_ROWS;
        export_preview_dirty = 1;
        break;
    case EV_ENTER:
    {
        if (export_preview_sel_idx >= export_disp_cnt)
            break;

        const char *desc = export_tables[export_preview_sel_idx].desc;
        const char *name = export_tables[export_preview_sel_idx].name;
        char usb_path[256] = {0};
        char dest[512] = {0};
        char msg[64] = {0};
        int to_usb = 0;

        strcpy(popup_title, "Exporting...");
        strcpy(popup_line1, desc);
        strcpy(popup_line2, "Please wait...");
        popup_line3[0] = 0;
        menu_current = export_popup_node;
        hal_oled_refresh();

        int mount_ret = find_usb_mountpoint(usb_path, sizeof(usb_path));
        if (mount_ret == 0)
        {
            snprintf(dest, sizeof(dest), "%s/%s.xlsx", usb_path, name);
            to_usb = 1;
        }
        else
        {
            snprintf(dest, sizeof(dest), "/mnt/msata/%s.xlsx", name);
        }

        int ret = db_export_xlsx(name, dest, msg, sizeof(msg));
        if (ret == 0 && to_usb)
        {
            system("sync");
            system("sync");
        }

        if (ret == 0 && to_usb)
        {
            strcpy(popup_title, "Export Done");
            snprintf(popup_line1, sizeof(popup_line1), "%s", desc);
            snprintf(popup_line2, sizeof(popup_line2), "Saved to USB");
            snprintf(popup_line3, sizeof(popup_line3), "Path: %s", usb_path);
        }
        else if (ret == 0)
        {
            strcpy(popup_title, "Export Done");
            snprintf(popup_line1, sizeof(popup_line1), "%s", desc);
            snprintf(popup_line2, sizeof(popup_line2), "Saved Local");
            strcpy(popup_line3, "/mnt/msata");
        }
        else
        {
            strcpy(popup_title, "Export Failed");
            snprintf(popup_line1, sizeof(popup_line1), "%s", desc);
            snprintf(popup_line2, sizeof(popup_line2), "Error: %s", msg);
            strcpy(popup_line3, to_usb ? "USB write failed" : "Local write failed");
        }
        hal_oled_refresh();

        usleep(2000000);

        if (to_usb)
        {
            safe_umount(usb_path);
        }

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

// ====================== 导出总入口 ======================
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

// ====================== 模式设置列表 ======================
static void page_mode_list_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Select Mode");
    hal_oled_line(0, 10, 127, 10);
    const char *modes[] = {"Balanced", "Performance"};
    int y = 25;
    for (int i = 0; i < 2; i++)
    {
        char line[32];
        snprintf(line, sizeof(line), "%s %s", i == mode_list_selected ? ">" : " ", modes[i]);
        hal_oled_string(0, y, line);
        y += 15;
    }
    hal_oled_refresh();
}

static void page_mode_list_handle_event(event_t ev)
{
    switch (ev)
    {
    case EV_UP:
    case EV_DOWN:
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

// ====================== 工具列表（主菜单） ======================
static void page_tools_list_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "System Tools");
    hal_oled_line(0, 10, 127, 10);

    const char *items[] = {"Shutdown", "Reboot", "Restart UI", "Mode Settings", "Export Data", "Data Analysis"};
    const int total_items = 6;
    const int items_per_page = 4;

    if (tools_list_selected < tools_list_offset)
    {
        tools_list_offset = tools_list_selected;
    }
    else if (tools_list_selected >= tools_list_offset + items_per_page)
    {
        tools_list_offset = tools_list_selected - items_per_page + 1;
    }

    int y = 18;
    for (int i = 0; i < items_per_page && (tools_list_offset + i) < total_items; i++)
    {
        int idx = tools_list_offset + i;
        char line[32];
        snprintf(line, sizeof(line), "%s %s", (idx == tools_list_selected) ? ">" : " ", items[idx]);
        hal_oled_string(0, y, line);
        y += 10;
    }

    if (tools_list_offset > 0)
    {
        hal_oled_string(120, 18, "^");
    }
    if (tools_list_offset + items_per_page < total_items)
    {
        hal_oled_string(120, 18 + (items_per_page - 1) * 10, "v");
    }

    hal_oled_refresh();
}

static void page_tools_list_handle_event(event_t ev)
{
    const int total_items = 6;
    switch (ev)
    {
    case EV_UP:
        tools_list_selected--;
        if (tools_list_selected < 0)
            tools_list_selected = total_items - 1;
        break;
    case EV_DOWN:
        tools_list_selected++;
        if (tools_list_selected >= total_items)
            tools_list_selected = 0;
        break;
    case EV_ENTER:
        menu_current = tools_list_node->children[tools_list_selected];
        break;
    case EV_BACK:
        menu_back();
        break;
    }
}

// ====================== 数据分析辅助函数 ======================
static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

static void calc_mean_std(const double *data, int n, double *mean, double *std)
{
    double sum = 0.0;
    for (int i = 0; i < n; i++)
        sum += data[i];
    *mean = sum / n;
    double var = 0.0;
    for (int i = 0; i < n; i++)
        var += (data[i] - *mean) * (data[i] - *mean);
    *std = sqrt(var / n);
}

static double calc_pearson(const double *x, const double *y, int n)
{
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0, sum_y2 = 0;
    for (int i = 0; i < n; i++)
    {
        sum_x += x[i];
        sum_y += y[i];
        sum_xy += x[i] * y[i];
        sum_x2 += x[i] * x[i];
        sum_y2 += y[i] * y[i];
    }
    double denom = (n * sum_x2 - sum_x * sum_x) * (n * sum_y2 - sum_y * sum_y);
    if (denom <= 0)
        return 0;
    return (n * sum_xy - sum_x * sum_y) / sqrt(denom);
}

static double calc_trend_per_day(const double *y, const double *x_days, int n)
{
    if (n < 2)
        return 0;

    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
    for (int i = 0; i < n; i++)
    {
        sum_x += x_days[i];
        sum_y += y[i];
        sum_xy += x_days[i] * y[i];
        sum_x2 += x_days[i] * x_days[i];
    }

    double denominator = n * sum_x2 - sum_x * sum_x;
    if (fabs(denominator) < 1e-9)
        return 0;

    return (n * sum_xy - sum_x * sum_y) / denominator; // 直接返回 ℃/天
}
static void update_analysis_display(void)
{
    memset(analysis_display_lines, 0, sizeof(analysis_display_lines));

    switch (analysis_page)
    {
    case 0: // 第1页：样本数、温度均值、温度标准差、温度范围
        snprintf(analysis_display_lines[0], 21, "Samples: %d", analysis_res.count);
        snprintf(analysis_display_lines[1], 21, "T mean: %.1f", analysis_res.temp_mean);
        snprintf(analysis_display_lines[2], 21, "T std: %.2f", analysis_res.temp_std);
        snprintf(analysis_display_lines[3], 21, "T range: %.1f-%.1f", analysis_res.temp_min, analysis_res.temp_max);
        break;
    case 1: // 第2页：湿度均值、湿度标准差、湿度范围、相关系数
        snprintf(analysis_display_lines[0], 21, "H mean: %.1f", analysis_res.humi_mean);
        snprintf(analysis_display_lines[1], 21, "H std: %.2f", analysis_res.humi_std);
        snprintf(analysis_display_lines[2], 21, "H range: %.0f-%.0f", analysis_res.humi_min, analysis_res.humi_max);
        snprintf(analysis_display_lines[3], 21, "Corr: %.3f", analysis_res.corr_pearson);
        break;
    case 2: // 第3页：THI、舒适度等级、温度异常数、湿度异常数
        snprintf(analysis_display_lines[0], 21, "THI: %.1f", analysis_res.thi_mean);
        snprintf(analysis_display_lines[1], 21, "Grade: %.10s", analysis_res.comfort_grade);
        snprintf(analysis_display_lines[2], 21, "T anomaly: %d", analysis_res.temp_anom_3sigma);
        snprintf(analysis_display_lines[3], 21, "H anomaly: %d", analysis_res.humi_anom_3sigma);
        break;
    case 3: // 第4页：温度趋势、湿度趋势、建议（截断为两行）
        snprintf(analysis_display_lines[0], 21, "T trend: %+.2f C/d", analysis_res.temp_trend_per_day);
        snprintf(analysis_display_lines[1], 21, "H trend: %+.2f %%/d", analysis_res.humi_trend_per_day);
        snprintf(analysis_display_lines[2], 21, "Sug: %.12s", analysis_res.comfort_suggestion);
        snprintf(analysis_display_lines[3], 21, " ");
        break;
    default:
        break;
    }
}

static void perform_data_analysis(void)
{
    double *temp = NULL, *humi = NULL, *days = NULL;
    int data_count = 0;

    if (db_get_sensor_analysis_data(&temp, &humi, &days, &data_count) != 0 || data_count == 0)
    {
        analysis_ready = -1;
        return;
    }

    analysis_result_t res = {0};
    res.count = data_count;

    calc_mean_std(temp, data_count, &res.temp_mean, &res.temp_std);
    calc_mean_std(humi, data_count, &res.humi_mean, &res.humi_std);

    res.temp_min = temp[0];
    res.temp_max = temp[0];
    res.humi_min = humi[0];
    res.humi_max = humi[0];
    for (int i = 1; i < data_count; i++)
    {
        if (temp[i] < res.temp_min)
            res.temp_min = temp[i];
        if (temp[i] > res.temp_max)
            res.temp_max = temp[i];
        if (humi[i] < res.humi_min)
            res.humi_min = humi[i];
        if (humi[i] > res.humi_max)
            res.humi_max = humi[i];
    }

    res.corr_pearson = calc_pearson(temp, humi, data_count);

    double thi_sum = 0;
    for (int i = 0; i < data_count; i++)
    {
        double thi = 0.8 * temp[i] + (0.01 * humi[i]) * (0.8 * temp[i] - 14.3) + 46.3;
        thi_sum += thi;
    }
    res.thi_mean = thi_sum / data_count;

    if (res.thi_mean < 55)
    {
        strcpy(res.comfort_grade, "Cold");
        strcpy(res.comfort_suggestion, "Warm");
    }
    else if (res.thi_mean < 60)
    {
        strcpy(res.comfort_grade, "Cool");
        strcpy(res.comfort_suggestion, "Add clothes");
    }
    else if (res.thi_mean < 65)
    {
        strcpy(res.comfort_grade, "Comfort");
        strcpy(res.comfort_suggestion, "Good");
    }
    else if (res.thi_mean < 70)
    {
        strcpy(res.comfort_grade, "Warm");
        strcpy(res.comfort_suggestion, "Ventilate");
    }
    else if (res.thi_mean < 75)
    {
        strcpy(res.comfort_grade, "Hot");
        strcpy(res.comfort_suggestion, "Fan/AC");
    }
    else
    {
        strcpy(res.comfort_grade, "Muggy");
        strcpy(res.comfort_suggestion, "Cool down");
    }

    // 3σ异常
    double temp_std3 = 3 * res.temp_std;
    double humi_std3 = 3 * res.humi_std;
    int temp_anom = 0, humi_anom = 0;
    for (int i = 0; i < data_count; i++)
    {
        if (fabs(temp[i] - res.temp_mean) > temp_std3)
            temp_anom++;
        if (fabs(humi[i] - res.humi_mean) > humi_std3)
            humi_anom++;
    }
    res.temp_anom_3sigma = temp_anom;
    res.humi_anom_3sigma = humi_anom;

    // 趋势计算（直接使用天数）
    res.temp_trend_per_day = calc_trend_per_day(temp, days, data_count);
    res.humi_trend_per_day = calc_trend_per_day(humi, days, data_count);

    // 调试输出（可选，编译后查看控制台）
    printf("DEBUG: Data count=%d, days range=%.2f to %.2f\n", data_count, days[0], days[data_count - 1]);
    printf("DEBUG: Temp trend=%.4f C/day, Humi trend=%.4f %%/day\n",
           res.temp_trend_per_day, res.humi_trend_per_day);

    free(temp);
    free(humi);
    free(days);

    analysis_res = res;
    analysis_page = 0;
    update_analysis_display();
    analysis_ready = 1;
}

// ====================== 数据分析页面 ======================
static void page_data_analysis_draw(void)
{
    if (!analysis_ready)
    {
        hal_oled_clear();
        hal_oled_string(0, 0, "Data Analysis");
        hal_oled_line(0, 10, 127, 10);
        hal_oled_string(10, 30, "Loading...");
        hal_oled_refresh();
        perform_data_analysis();
        if (analysis_ready != 1)
        {
            hal_oled_clear();
            hal_oled_string(0, 0, "Data Analysis");
            hal_oled_line(0, 10, 127, 10);
            hal_oled_string(10, 30, "No data");
            hal_oled_refresh();
            return;
        }
        update_analysis_display();
    }

    hal_oled_clear();
    hal_oled_string(0, 0, "Data Analysis");
    hal_oled_line(0, 10, 127, 10);

    // 页码指示
    char page_indicator[16];
    snprintf(page_indicator, sizeof(page_indicator), "Pg%d/%d", analysis_page + 1, analysis_total_pages);
    int x = OLED_WIDTH - strlen(page_indicator) * CHAR_WIDTH;
    hal_oled_string(x, 0, page_indicator);

    int y = 18;
    for (int i = 0; i < 4 && analysis_display_lines[i][0] != '\0'; i++)
    {
        hal_oled_string(0, y, analysis_display_lines[i]);
        y += 10;
    }

    // 已移除底部提示行
    hal_oled_refresh();
}

static void page_data_analysis_handle_event(event_t ev)
{
    switch (ev)
    {
    case EV_UP:
        if (analysis_page > 0)
        {
            analysis_page--;
            update_analysis_display();
        }
        break;
    case EV_DOWN:
        if (analysis_page < analysis_total_pages - 1)
        {
            analysis_page++;
            update_analysis_display();
        }
        break;
    case EV_BACK:
        analysis_ready = 0;
        analysis_page = 0;
        menu_back();
        break;
    default:
        break;
    }
}

// ====================== 初始化与菜单创建 ======================
void page_tools_init(void)
{
    static int init = 0;
    if (init)
        return;

    page_register("Confirm Action", page_tools_confirm_draw, page_tools_confirm_handle_event);
    page_register("Tools", page_tools_list_draw, page_tools_list_handle_event);
    page_register("Shutdown", page_tools_shutdown_draw, page_tools_shutdown_handle_event);
    page_register("Reboot", page_tools_reboot_draw, page_tools_reboot_handle_event);
    page_register("Restart UI", page_restart_ui_draw, page_restart_ui_handle_event);
    page_register("Mode Settings", page_mode_list_draw, page_mode_list_handle_event);
    page_register("Export Data", page_tools_export_draw, page_tools_export_handle_event);
    page_register("Export Popup", page_export_popup_draw, page_export_popup_handle_event);
    page_register("Data Analysis", page_data_analysis_draw, page_data_analysis_handle_event);

    init = 1;
}

menu_item_t *page_tools_create_menu(void)
{
    page_tools_init();
    if (!tools_list_node)
    {
        extern const unsigned char icon_tools_28x28[];

        tools_list_node = menu_create("Tools", page_tools_list_draw, icon_tools_28x28);
        shutdown_node = menu_create("Shutdown", page_tools_shutdown_draw, NULL);
        reboot_node = menu_create("Reboot", page_tools_reboot_draw, NULL);
        restart_ui_node = menu_create("Restart UI", page_restart_ui_draw, NULL);
        mode_node = menu_create("Mode Settings", page_mode_list_draw, NULL);
        confirm_node = menu_create("Confirm Action", page_tools_confirm_draw, NULL);
        export_node = menu_create("Export Data", page_tools_export_draw, NULL);
        export_popup_node = menu_create("Export Popup", page_export_popup_draw, NULL);
        analysis_node = menu_create("Data Analysis", page_data_analysis_draw, NULL);

        menu_add_child(tools_list_node, shutdown_node);
        menu_add_child(tools_list_node, reboot_node);
        menu_add_child(tools_list_node, restart_ui_node);
        menu_add_child(tools_list_node, mode_node);
        menu_add_child(tools_list_node, export_node);
        menu_add_child(tools_list_node, analysis_node);
        menu_add_child(export_node, export_popup_node);
    }
    return tools_list_node;
}