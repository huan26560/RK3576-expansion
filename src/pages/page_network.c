#include <stdio.h>
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
static int lan_scroll_offset = 0;              // 当前列表滚动偏移量
static const char *last_drawn_lan_menu = NULL; // 用于检测是否首次进入LAN Scan页面
#define OLED_WIDTH 128
#define CHAR_WIDTH 6
#define SPEEDTEST_TIMEOUT 15      // 测速超时时间（秒）
#define SPEEDTEST_PROGRESS_STEP 5 // 进度条更新步长
#define SPEEDTEST_SERVER_ID "16204"
extern unsigned long millis(void);

// 在文件顶部
static int show_ping_result = 0;
static char ping_result[64] = {0};
static int selected_device = 0; // 当前选中的设备索引
static int list_selected = 0;   // 列表页选中项
static int status_page_index = 0;
static int lan_page_index = 0;    // 这个现在没用了，可以删除
static int show_ping_confirm = 0; // 这个也删了，不需要确认页

typedef struct
{
    int in_progress;       // 是否正在测速
    int progress;          // 进度百分比
    char result[128];      // 扩容结果缓冲区（显示下载+上传）
    uint64_t start_time;   // 测速开始时间（毫秒）
    double current_speed;  // 实时速度（仅模拟）
    pthread_mutex_t lock;  // 线程锁
    pthread_t thread;      // 测速线程ID
    int thread_running;    // 线程是否运行中
    double download_speed; // 下载速度
    double upload_speed;   // 上传速度
    float ping_ms;         // Ping值
} speedtest_state_t;

static speedtest_state_t speedtest = {
    .in_progress = 0,
    .progress = 0,
    .result = {0},
    .start_time = 0,
    .current_speed = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .thread = 0,
    .thread_running = 0};

static menu_item_t *net_root = NULL;
static menu_item_t *list_node = NULL;
static menu_item_t *status_node = NULL;
static menu_item_t *speedtest_node = NULL;
static menu_item_t *lan_scan_node = NULL;

extern menu_item_t *menu_current;

// 工具函数
static int ping_device(const char *ip)
{

    char cmd[128];
    snprintf(cmd, sizeof(cmd), "ping -c 1 -W 2 %s 2>&1 | grep 'time=' | cut -d= -f4 | cut -d' ' -f1", ip);

    char result[32] = {0};
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return -1;

    int n = fread(result, 1, sizeof(result) - 1, fp);
    result[n] = 0;
    pclose(fp);

    int latency = (result[0] >= '0' && result[0] <= '9') ? atoi(result) : -1;
    printf("[UI] Ping result: %d ms\n", latency);
    return latency;
}

// 速度测试线程
static void *speedtest_thread(void *arg)
{
    (void)arg;
    // 1. 构造测速命令（去掉--no-upload，指定国内服务器）
    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "speedtest-cli --simple --server %s --timeout 25 2>&1",
             SPEEDTEST_SERVER_ID); // 指定服务器，增加命令级超时

    double download = 0.0, upload = 0.0;
    float ping = 0.0;
    int has_valid_data = 0;

    FILE *fp = popen(cmd, "r");
    if (!fp)
    {
        pthread_mutex_lock(&speedtest.lock);
        snprintf(speedtest.result, sizeof(speedtest.result), "Error: Command not found");
        speedtest.thread_running = 0;
        pthread_mutex_unlock(&speedtest.lock);
        return NULL;
    }

    // 2. 解析Ping、Download、Upload（兼容中英文输出）
    char line[128];
    while (fgets(line, sizeof(line), fp) && speedtest.thread_running)
    {
        printf("[SpeedTest] %s", line); // 打印原始日志，方便排查

        // 解析Ping值
        if (strstr(line, "Ping") || strstr(line, "延迟"))
        {
            char *val = strchr(line, ':');
            if (val)
            {
                val += 2;
                ping = atof(val);
                speedtest.ping_ms = ping;
            }
        }
        // 解析下载速度
        else if (strstr(line, "Download") || strstr(line, "下载"))
        {
            char *val = strchr(line, ':');
            if (val)
            {
                val += 2;
                download = atof(val);
                if (download > 0)
                    has_valid_data = 1;
            }
        }
        // 解析上传速度
        else if (strstr(line, "Upload") || strstr(line, "上传"))
        {
            char *val = strchr(line, ':');
            if (val)
            {
                val += 2;
                upload = atof(val);
                if (upload > 0)
                    has_valid_data = 1;
            }
        }
        // 捕获错误
        else if (strstr(line, "error") || strstr(line, "failed") || strstr(line, "timeout"))
        {
            pthread_mutex_lock(&speedtest.lock);
            snprintf(speedtest.result, sizeof(speedtest.result), "Error: %.40s", line);
            pthread_mutex_unlock(&speedtest.lock);
            break;
        }
    }
    pclose(fp);

    // 3. 处理0值情况（友好提示）
    pthread_mutex_lock(&speedtest.lock);
    speedtest.download_speed = download;
    speedtest.upload_speed = upload;

    if (has_valid_data)
    {
        // 显示Ping+下载+上传
        snprintf(speedtest.result, sizeof(speedtest.result),
                 "Ping:%.1fms DL:%.1f UL:%.1f Mbps",
                 ping, download, upload);
    }
    else
    {
        // 0值友好提示（说明不是代码问题，是网络限制）
        if (ping > 0)
        {
            snprintf(speedtest.result, sizeof(speedtest.result),
                     "Ping:%.1fms No bandwidth data (network limit)", ping);
        }
        else
        {
            snprintf(speedtest.result, sizeof(speedtest.result),
                     "No valid data (check network/server)");
        }
    }

    speedtest.progress = 100;
    speedtest.in_progress = 0;
    speedtest.thread_running = 0;
    pthread_mutex_unlock(&speedtest.lock);

    return NULL;
}

static void update_speedtest_progress(void)
{
    if (!speedtest.in_progress)
        return;

    pthread_mutex_lock(&speedtest.lock);
    uint64_t elapsed = (millis() - speedtest.start_time) / 1000;

    // 超时处理（替换 pthread_timedjoin_np 为标准 pthread_join）
    if (elapsed >= SPEEDTEST_TIMEOUT)
    {
        if (speedtest.thread_running)
        {
            printf("[UI] Speed test timeout, cancel thread\n");
            pthread_cancel(speedtest.thread);
            void *ret;
            // 替换为标准 pthread_join（阻塞等待线程终止，无超时）
            pthread_join(speedtest.thread, &ret);
        }
        snprintf(speedtest.result, sizeof(speedtest.result), "Timeout (≥%ds)", SPEEDTEST_TIMEOUT);
        speedtest.in_progress = 0;
        speedtest.thread_running = 0;
        speedtest.progress = 100;
        pthread_mutex_unlock(&speedtest.lock); // 修正原代码的锁对象错误
        return;
    }

    // 剩余进度计算逻辑不变...
    int new_progress;
    if (elapsed < 5)
        new_progress = elapsed * 10;
    else if (elapsed < 20)
        new_progress = 50 + (elapsed - 5) * 2;
    else
        new_progress = 80 + (elapsed - 20) * 1;
    new_progress = new_progress > 100 ? 100 : new_progress;

    if (new_progress > speedtest.progress)
    {
        speedtest.progress = new_progress;
        speedtest.current_speed = (double)(rand() % 40 + 10) / 2.0;
    }
    pthread_mutex_unlock(&speedtest.lock);
}

static void run_speedtest(void)
{
    pthread_mutex_lock(&speedtest.lock);
    if (speedtest.in_progress)
    {
        printf("[UI] Speed test already running\n");
        pthread_mutex_unlock(&speedtest.lock);
        return;
    }

    // 重置所有状态（包括Ping、上传下载）
    memset(speedtest.result, 0, sizeof(speedtest.result));
    speedtest.progress = 0;
    speedtest.start_time = millis();
    speedtest.in_progress = 1;
    speedtest.thread_running = 1;
    speedtest.current_speed = 0.0;
    speedtest.download_speed = 0.0;
    speedtest.upload_speed = 0.0;
    speedtest.ping_ms = 0.0;
    snprintf(speedtest.result, sizeof(speedtest.result), "Testing...");
    pthread_mutex_unlock(&speedtest.lock);

    printf("[UI] Starting speed test (server: %s, timeout: %ds)\n", SPEEDTEST_SERVER_ID, SPEEDTEST_TIMEOUT);

    // 创建线程
    int ret = pthread_create(&speedtest.thread, NULL, speedtest_thread, NULL);
    if (ret != 0)
    {
        pthread_mutex_lock(&speedtest.lock);
        snprintf(speedtest.result, sizeof(speedtest.result), "Error: Thread create failed");
        speedtest.in_progress = 0;
        speedtest.thread_running = 0;
        pthread_mutex_unlock(&speedtest.lock);
        printf("[UI] Speed test thread create failed: %d\n", ret);
        return;
    }
    pthread_detach(speedtest.thread);
}

// 页面绘制
static void page_network_root_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Network Manager");
    hal_oled_line(0, 10, 127, 10);

    network_state_t *ns = &g_network_state;
    char ssid[32] = {0}, ip[32] = {0};
    int connected = 0, count = 0;

    pthread_mutex_lock(&ns->lock);
    strncpy(ssid, ns->wifi_ssid, sizeof(ssid) - 1);
    strncpy(ip, ns->wifi_ip, sizeof(ip) - 1);
    connected = ns->wifi_connected;
    count = ns->lan_device_count;
    pthread_mutex_unlock(&ns->lock);

    char buf[64];
    snprintf(buf, sizeof(buf), "WiFi: %s", connected ? "Connected" : "Disconnected");
    hal_oled_string(0, 20, buf);

    if (ip[0])
    {
        snprintf(buf, sizeof(buf), "IP: %s", ip);
        hal_oled_string(0, 30, buf);
    }

    if (ssid[0])
    {
        snprintf(buf, sizeof(buf), "SSID: %.15s", ssid);
        hal_oled_string(0, 40, buf);
    }

    snprintf(buf, sizeof(buf), "LAN: %d devices", count);
    hal_oled_string(0, 50, buf);

    hal_oled_refresh();
}

static void page_network_list_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Network Tools");
    hal_oled_line(0, 10, 127, 10);

    const char *items[] = {"Status", "SpeedTest", "LAN Scan"};
    int count = 3;

    int y = 18;
    for (int i = 0; i < count; i++)
    {
        char line[32];
        snprintf(line, sizeof(line), "%s %s", i == list_selected ? ">" : " ", items[i]);
        hal_oled_string(0, y, line);
        y += 10;
    }

    hal_oled_refresh();
}

static void page_network_status_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Network Status");
    hal_oled_line(0, 10, 127, 10);

    network_state_t *ns = &g_network_state;
    char buf[128];
    int y = 18;

    pthread_mutex_lock(&ns->lock);

    switch (status_page_index)
    {
    case 0: // WiFi
        snprintf(buf, sizeof(buf), "SSID:%.25s", ns->wifi_ssid[0] ? ns->wifi_ssid : "N/A");
        hal_oled_string(0, y, buf);
        y += 10;
        snprintf(buf, sizeof(buf), "IP:%.30s", ns->wifi_ip[0] ? ns->wifi_ip : "N/A");
        hal_oled_string(0, y, buf);
        y += 10;
        snprintf(buf, sizeof(buf), "Signal:%d dBm", ns->wifi_signal);
        hal_oled_string(0, y, buf);
        y += 10;
        snprintf(buf, sizeof(buf), "Rate:%d/%d Mbps", ns->wifi_tx_rate, ns->wifi_rx_rate);
        hal_oled_string(0, y, buf);
        y += 10;
        break;

    case 1: // 系统

        FILE *fp = popen("ifconfig wlan0 | grep 'RX packets' | awk '{print \"RX:\"$2\" \"$3}'", "r");
        if (fp)
        {
            fgets(buf, sizeof(buf), fp);
            hal_oled_string(0, y, buf);
            y += 10;
            pclose(fp);
        }

        fp = popen("ifconfig wlan0 | grep 'TX packets' | awk '{print \"TX:\"$2\" \"$3}'", "r");
        if (fp)
        {
            fgets(buf, sizeof(buf), fp);
            hal_oled_string(0, y, buf);
            y += 10;
            pclose(fp);
        }

        fp = popen("ip route | grep default | awk '{print \"GW:\"$3}'", "r");
        if (fp)
        {
            fgets(buf, sizeof(buf), fp);
            buf[strcspn(buf, "\n")] = 0;
            hal_oled_string(0, y, buf);
            y += 10;
            pclose(fp);
        }
        break;
    }

    pthread_mutex_unlock(&ns->lock);

    hal_oled_refresh();
}

static void page_network_speedtest_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Speed Test");
    hal_oled_line(0, 10, 127, 10);

    int y = 20;

    update_speedtest_progress();

    pthread_mutex_lock(&speedtest.lock);
    if (speedtest.in_progress)
    {
        // 显示模拟实时速度
        char speed_str[32];
        snprintf(speed_str, sizeof(speed_str), "%.1f Mbps", speedtest.current_speed);
        int x = (OLED_WIDTH - strlen(speed_str) * CHAR_WIDTH) / 2;
        hal_oled_string(x, y, speed_str);

        y += 15;
        // 绘制进度条
        int bar_width = 100, bar_height = 6;
        int bar_x = (OLED_WIDTH - bar_width) / 2;
        hal_oled_rect(bar_x, y, bar_x + bar_width, y + bar_height, 1);

        int fill = (speedtest.progress * bar_width) / 100;
        fill = fill > bar_width ? bar_width : (fill < 0 ? 0 : fill);
        if (fill > 0)
        {
            hal_oled_fill_rect(bar_x + 1, y + 1, bar_x + fill - 1, y + bar_height - 1, 0);
        }

        // 进度百分比
        char prog[16];
        snprintf(prog, sizeof(prog), "%d%%", speedtest.progress);
        x = (OLED_WIDTH - strlen(prog) * CHAR_WIDTH) / 2;
        hal_oled_string(x, y + bar_height + 2, prog);
    }
    else
    {
        // 显示结果（自动换行适配长文本）
        char *result = speedtest.result;
        int x = 0;
        // 如果文本过长，拆分为两行显示
        if (strlen(result) * CHAR_WIDTH > OLED_WIDTH)
        {
            // 第一行显示前半部分
            char line1[64];
            strncpy(line1, result, OLED_WIDTH / CHAR_WIDTH);
            line1[OLED_WIDTH / CHAR_WIDTH] = 0;
            x = (OLED_WIDTH - strlen(line1) * CHAR_WIDTH) / 2;
            hal_oled_string(x, y, line1);

            // 第二行显示后半部分
            char line2[64] = {0};
            strcpy(line2, result + strlen(line1));
            x = (OLED_WIDTH - strlen(line2) * CHAR_WIDTH) / 2;
            hal_oled_string(x, y + 10, line2);
        }
        else
        {
            x = (OLED_WIDTH - strlen(result) * CHAR_WIDTH) / 2;
            hal_oled_string(x, y + 5, result);
        }
    }

    // 操作提示
    if (!speedtest.in_progress)
    {
        hal_oled_string(0, 55, "ENTER: Start");
    }
    pthread_mutex_unlock(&speedtest.lock);

    hal_oled_refresh();
}

static void page_network_lan_draw(void)
{
    // 检测是否首次进入 LAN Scan 页面（从其他菜单切换过来）
    if (last_drawn_lan_menu != menu_current->name &&
        strcmp(menu_current->name, "LAN Scan") == 0)
    {
        selected_device = 0;
        lan_scroll_offset = 0;
    }
    last_drawn_lan_menu = menu_current->name;

    // 如果处于显示 ping 结果状态，则显示结果页面
    if (show_ping_result)
    {
        hal_oled_clear();
        hal_oled_string(0, 0, "Ping Result");
        hal_oled_line(0, 10, 127, 10);

        int x = (OLED_WIDTH - strlen(ping_result) * CHAR_WIDTH) / 2;
        int y = 30;
        hal_oled_string(x, y, ping_result);
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
    for (int i = 0; i < count; i++)
    {
        strncpy(devices[i], ns->lan_devices[i], sizeof(devices[0]) - 1);
    }
    pthread_mutex_unlock(&ns->lock);

    if (count == 0)
    {
        hal_oled_string(0, 25, "No devices found");
        hal_oled_string(0, 40, "Press ENTER to scan");
    }
    else
    {
        // 确保选中项和滚动偏移量有效
        if (selected_device < 0)
            selected_device = 0;
        if (selected_device >= count)
            selected_device = count - 1;

        // 调整滚动偏移，使选中项可见
        if (selected_device < lan_scroll_offset)
        {
            lan_scroll_offset = selected_device;
        }
        else if (selected_device >= lan_scroll_offset + 4)
        {
            lan_scroll_offset = selected_device - 3;
        }
        // 边界保护
        if (lan_scroll_offset < 0)
            lan_scroll_offset = 0;
        if (lan_scroll_offset > count - 4)
            lan_scroll_offset = count - 4;
        if (lan_scroll_offset < 0)
            lan_scroll_offset = 0; // 当 count<4 时再次归零

        int y = 18;
        int display_count = (count - lan_scroll_offset) > 4 ? 4 : (count - lan_scroll_offset);
        for (int i = 0; i < display_count; i++)
        {
            int idx = lan_scroll_offset + i;
            char line[64];
            // 序号显示为全局序号（lan_scroll_offset + i + 1）
            snprintf(line, sizeof(line), "%s %d: %s",
                     idx == selected_device ? ">" : " ",
                     idx + 1,
                     devices[idx]);
            hal_oled_string(0, y, line);
            y += 10;
        }

        // 如果总设备数超过4，在右下角显示当前位置
        if (count > 4)
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d/%d", selected_device + 1, count);
            int x = OLED_WIDTH - strlen(buf) * CHAR_WIDTH;
            hal_oled_string(x, 55, buf);
        }
    }
    hal_oled_refresh();
}
// 事件处理
void page_network_handle_event(event_t ev)
{
    const char *name = menu_current->name;

    if (strcmp(name, "Network") == 0)
    {
        if (ev == EV_ENTER)
        {
            menu_current = list_node;
            list_selected = 0;
        }
        else if (ev == EV_BACK)
        {
            extern void menu_back(void);
            menu_back();
        }
    }
    else if (strcmp(name, "Network Tools") == 0)
    {
        switch (ev)
        {
        case EV_UP:
            list_selected--;
            if (list_selected < 0)
                list_selected = 2;
            break;
        case EV_DOWN:
            list_selected++;
            if (list_selected > 2)
                list_selected = 0;
            break;
        case EV_ENTER:
            menu_current = list_node->children[list_selected];
            break;
        case EV_BACK:
            extern void menu_back(void);
            menu_back();
            break;
        }
    }
    else if (strcmp(name, "Status") == 0)
    {
        switch (ev)
        {
        case EV_UP:
            status_page_index--;
            if (status_page_index < 0)
                status_page_index = 1;
            break;
        case EV_DOWN:
            status_page_index++;
            if (status_page_index > 1)
                status_page_index = 0;
            break;
        case EV_BACK:
            status_page_index = 0;
            extern void menu_back(void);
            menu_back();
            break;
        }
    }
    else if (strcmp(name, "SpeedTest") == 0)
    {
        // 增强：按下 BACK 时终止正在进行的测速
        if (ev == EV_BACK)
        {
            pthread_mutex_lock(&speedtest.lock);
            if (speedtest.in_progress)
            {
                // 终止线程并重置状态
                if (speedtest.thread_running)
                {
                    pthread_cancel(speedtest.thread);
                    pthread_join(speedtest.thread, NULL);
                }
                snprintf(speedtest.result, sizeof(speedtest.result), "Test cancelled");
                speedtest.in_progress = 0;
                speedtest.thread_running = 0;
                speedtest.progress = 0;
            }
            pthread_mutex_unlock(&speedtest.lock);

            extern void menu_back(void);
            menu_back();
        }
        // 原有逻辑：ENTER 启动测速（仅非运行中）
        else if (ev == EV_ENTER && !speedtest.in_progress)
        {
            run_speedtest();
        }
    }
    else if (strcmp(name, "LAN Scan") == 0)
    {
        if (show_ping_result)
        {
            if (ev == EV_BACK)
            {
                show_ping_result = 0;
            }
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
            {
                selected_device--;
                if (selected_device < 0)
                {
                    selected_device = count - 1; // 循环到底部
                }
                // 滚动偏移会在 draw 中自动调整，无需手动干预
            }
            break;

        case EV_DOWN:
            if (count > 0)
            {
                selected_device++;
                if (selected_device >= count)
                {
                    selected_device = 0; // 循环到顶部
                }
            }
            break;

        case EV_ENTER:
            if (count == 0)
            {
                // 无设备时触发扫描
                network_trigger_lan_scan();
                // 扫描后列表可能更新，但需等待异步完成，此处先保持选中为0
                selected_device = 0;
                lan_scroll_offset = 0;
            }
            else
            {
                // 有设备时 ping 选中的设备
                char ip[32] = {0};
                pthread_mutex_lock(&ns->lock);
                if (selected_device >= 0 && selected_device < count)
                {
                    strncpy(ip, ns->lan_devices[selected_device], sizeof(ip) - 1);
                }
                pthread_mutex_unlock(&ns->lock);

                if (ip[0])
                {
                    int latency = ping_device(ip);
                    show_ping_result = 1;
                    if (latency > 0)
                    {
                        snprintf(ping_result, sizeof(ping_result), "%s: %dms", ip, latency);
                    }
                    else
                    {
                        snprintf(ping_result, sizeof(ping_result), "%s: Failed", ip);
                    }
                }
            }
            break;

        case EV_BACK:
            // 返回上一级菜单
            extern void menu_back(void);
            menu_back();
            break;
        }
    }
}

void page_network_init(void)
{
    static int initialized = 0;
    if (initialized)
        return;

    network_monitor_init();

    page_register("Network", page_network_root_draw, page_network_handle_event);
    page_register("Network Tools", page_network_list_draw, page_network_handle_event);
    page_register("Status", page_network_status_draw, page_network_handle_event);
    page_register("SpeedTest", page_network_speedtest_draw, page_network_handle_event);
    page_register("LAN Scan", page_network_lan_draw, page_network_handle_event);

    initialized = 1;
}

menu_item_t *page_network_create_menu(void)
{
    page_network_init();

    if (net_root == NULL)
    {
        // ✅ 修复：确保图标存在
        extern const unsigned char icon_network_28x28[];

        net_root = menu_create(" Network", page_network_root_draw, icon_network_28x28);
        list_node = menu_create("Network Tools", page_network_list_draw, NULL);
        status_node = menu_create("Status", page_network_status_draw, NULL);
        speedtest_node = menu_create("SpeedTest", page_network_speedtest_draw, NULL);
        lan_scan_node = menu_create("LAN Scan", page_network_lan_draw, NULL);

        menu_add_child(net_root, list_node);
        menu_add_child(list_node, status_node);
        menu_add_child(list_node, speedtest_node);
        menu_add_child(list_node, lan_scan_node);

        printf("[UI] Network menu tree created\n");
    }

    return net_root;
}