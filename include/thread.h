#ifndef THREADS_H
#define THREADS_H

#include <pthread.h>
#include <stdint.h>  // 新增：用于结构体成员类型

// ========== 原有声明（完全保留） ==========
// 线程函数声明
void threads_init(void);
static void create_thread(pthread_t *t, void* (*f)(void*), const char *name) ;
void *system_publish_thread(void *arg) ;
void *mqtt_thread(void *arg);
void *sensor_thread(void *arg);
void *network_publish_thread(void *arg);
void *button_thread(void *arg);

// ========== 新增：天气相关接口（供page_env.c调用） ==========
// 天气数据结构体
typedef struct {
    char weather_text[20];
    float temp;
    float humi;
    char update_time[20];
    int weather_code;
    int is_valid;
} network_weather_t;

// 天气线程函数声明（内部使用，供threads.c创建线程）
void *weather_worker_thread(void *arg);

// 对外接口：触发天气刷新（非阻塞）
void trigger_weather_refresh(void);

// 对外接口：获取天气数据（线程安全）
void get_weather_data(network_weather_t *out_weather);

// 对外接口：获取经纬度（线程安全）
void get_weather_location(char *lat, char *lon, int buf_size);
void get_weather_address(char *addr, int buf_size);
#endif