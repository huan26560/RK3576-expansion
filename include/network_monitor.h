#ifndef NETWORK_MONITOR_H
#define NETWORK_MONITOR_H

#define MAX_LAN_DEVICES 20
#include "pthread.h"
typedef struct {
    // WiFi信息
    char wifi_ssid[64];
    char wifi_ip[32];
    int wifi_signal;      // dBm
    int wifi_freq;        // MHz
    int wifi_tx_rate;     // Mbps
    int wifi_rx_rate;     // Mbps
    int wifi_connected;
    int wifi_internet;    // 新增：外网连通性
    
    // LAN设备
    char lan_devices[MAX_LAN_DEVICES][32];
    int lan_device_count;
    
    // 速度测试
    char speed_result[64];
    int speed_in_progress;
    
    // 线程同步
    pthread_mutex_t lock;
} network_state_t;

extern network_state_t g_network_state;

void network_monitor_init(void);
void network_trigger_lan_scan(void);
void network_monitor_get_state(network_state_t *state);

#endif