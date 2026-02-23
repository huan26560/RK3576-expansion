#ifndef THREADS_H
#define THREADS_H

#include <pthread.h>
#include <stdint.h>  // 用于结构体成员类型

// ========== 核心线程管理接口（对外公开） ==========
// 初始化所有线程（优化后：创建MQTT/数据发布/按键/天气线程）
void threads_init(void);

// 新增：优雅停止所有线程（安全退出）
void threads_stop(void);

// ========== 线程函数声明（内部使用，仅供threads.c创建线程） ==========
// MQTT客户端线程（保留）
void *mqtt_thread(void *arg);

// 合并后的统一数据发布线程（替代原sensor/system/network发布线程）
void *data_publish_thread(void *arg);

// 按键处理线程（保留）
void *button_thread(void *arg);

// 天气工作线程（保留）
void *weather_worker_thread(void *arg);

// ========== 天气相关对外接口（供page_env.c调用，完全保留） ==========
// 天气数据结构体
typedef struct {
    char weather_text[20];
    float temp;
    float humi;
    char update_time[20];
    int weather_code;
    int is_valid;
} network_weather_t;

// 触发天气异步刷新（非阻塞）
void trigger_weather_refresh(void);

// 获取天气数据（线程安全，需传入结构体指针）
void get_weather_data(network_weather_t *out_weather);

// 获取经纬度（线程安全，buf_size为传入数组的长度）
void get_weather_location(char *lat, char *lon, int buf_size);

// 获取拼音地址（线程安全，buf_size为传入数组的长度）
void get_weather_address(char *addr, int buf_size);

#endif // THREADS_H