#include "network_monitor.h"
#include "hal_system.h"  // 新增：使用底层硬件接口
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <pthread.h>

network_state_t g_network_state = {0};

// 内部函数
static void update_wifi_info(void);
static void scan_lan_devices(void);
static int exec_cmd(const char *cmd, char *output, int len);

// 命令执行工具
static int exec_cmd(const char *cmd, char *output, int len) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    int n = fread(output, 1, len - 1, fp);
    output[n] = 0;
    pclose(fp);
    return n;
}

// 更新WiFi信息（整合hal_system）
static void update_wifi_info(void) {
    network_state_t *ns = &g_network_state;
    char ssid[64] = {0}, ip[32] = {0};
    int signal = -999, freq = 0, rx = 0, tx = 0;
    
    // 使用iw获取详细信息
    char result[1024] = {0};
    if (exec_cmd("iw dev wlan0 link 2>&1", result, sizeof(result)) > 0) {
        char *line = strtok(result, "\n");
        while (line) {
            if (strstr(line, "SSID:")) {
                char *p = strchr(line, ':'); if (p) { p++; while(*p==' ') p++; strncpy(ssid, p, 63); }
            }
            else if (strstr(line, "signal:")) sscanf(line, "%*s %d", &signal);
            else if (strstr(line, "freq:")) sscanf(line, "%*s %d", &freq);
            else if (strstr(line, "rx bitrate:")) sscanf(line, "%*s %*s %d", &rx);
            else if (strstr(line, "tx bitrate:")) sscanf(line, "%*s %*s %d", &tx);
            line = strtok(NULL, "\n");
        }
    }
    
    // 获取IP地址
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        struct ifreq ifr;
        strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ-1);
        if (ioctl(sock, SIOCGIFADDR, &ifr) == 0) {
            struct sockaddr_in *addr = (struct sockaddr_in *)&ifr.ifr_addr;
            strncpy(ip, inet_ntoa(addr->sin_addr), 31);
        }
        close(sock);
    }
    
    // 更新状态（加锁）
    pthread_mutex_lock(&ns->lock);
    strncpy(ns->wifi_ssid, ssid, 63);
    strncpy(ns->wifi_ip, ip, 31);
    ns->wifi_signal = signal;
    ns->wifi_freq = freq;
    ns->wifi_rx_rate = rx;
    ns->wifi_tx_rate = tx;
    ns->wifi_connected = (ssid[0] != 0);
    ns->wifi_internet = hal_net_connected();  // 使用HAL层检测
    pthread_mutex_unlock(&ns->lock);
}

// 扫描LAN设备
static void scan_lan_devices(void) {
    network_state_t *ns = &g_network_state;
    
    char cmd[] = "arp -a 2>&1 | grep -oE '\\([0-9.]+\\)' | tr -d '()' | head -20";
    char output[1024] = {0};
    
    FILE *fp = popen(cmd, "r");
    if (!fp) return;
    int n = fread(output, 1, sizeof(output)-1, fp);
    output[n] = 0;
    pclose(fp);
    
    int count = 0;
    char *line = strtok(output, "\n");
    pthread_mutex_lock(&ns->lock);
    while (line && count < MAX_LAN_DEVICES) {
        if (strlen(line) >= 7) {
            strncpy(ns->lan_devices[count], line, 31);
            count++;
        }
        line = strtok(NULL, "\n");
    }
    ns->lan_device_count = count;
    pthread_mutex_unlock(&ns->lock);
    
}

// 后台线程
static void* network_monitor_thread(void *arg) {
    (void)arg;
    while (1) {
        update_wifi_info();
        scan_lan_devices();
        sleep(5);
    }
    return NULL;
}

// 初始化
void network_monitor_init(void) {
    static int initialized = 0;
    if (initialized) return;
    
    memset(&g_network_state, 0, sizeof(g_network_state));
    pthread_mutex_init(&g_network_state.lock, NULL);
    
    // 首次更新
    update_wifi_info();
    scan_lan_devices();
    
    // 创建线程
    pthread_t thread;
    pthread_create(&thread, NULL, network_monitor_thread, NULL);
    pthread_detach(thread);
    
    initialized = 1;
}

// 获取状态副本
void network_monitor_get_state(network_state_t *state) {
    if (!state) return;
    pthread_mutex_lock(&g_network_state.lock);
    memcpy(state, &g_network_state, sizeof(network_state_t));
    pthread_mutex_unlock(&g_network_state.lock);
}

// 触发扫描
void network_trigger_lan_scan(void) {
    scan_lan_devices();
}