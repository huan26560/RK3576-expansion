#ifndef HAL_SYSTEM_H
#define HAL_SYSTEM_H

// 添加这两个函数声明
void hal_system_init(void);
void hal_system_cleanup(void);

// CPU/内存/温度
int hal_cpu_usage(void);
float hal_cpu_temp(void);
int hal_mem_usage(void);

// WiFi信号强度（底层）
int hal_wifi_signal(void);

// 网络连通性（底层）
int hal_net_connected(void);

// 公共工具函数
unsigned long millis(void);
int hal_uptime_minutes(void);
#endif