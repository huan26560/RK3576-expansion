#ifndef SYSTEM_MONITOR_H
#define SYSTEM_MONITOR_H

#define MAX_CPU_CORES 8

#include "pthread.h"

typedef enum
{
    CORE_LITTLE = 0,
    CORE_BIG = 1,
    CORE_GPU = 2,
    CORE_NPU = 3
} core_type_t;

typedef struct
{
    char name[8];
    core_type_t type;
    int freq_mhz;
    int usage_percent;
    int temp_c;
} core_info_t;

typedef struct
{
    int total_mb;
    int free_mb;
    int used_percent;
    int available_mb;
    int buffers_mb;
    int cached_mb;
} memory_info_t;

typedef struct
{
    core_info_t cpu_cores[MAX_CPU_CORES];
    int cpu_core_count;
    int cpu_total_usage;

    struct
    {
        int freq_mhz;
        int usage_percent;
        int temp_c;
    } gpu;

    struct
    {
        int freq_mhz;      // NPU频率（MHz）
        int usage_percent; // 总使用率
        int core_count;    // NPU核心数
        int core_usage[2]; // 每个核心的使用率（支持Core0/Core1）
        int temp_c;        // 温度（℃）
    } npu;

    int uptime_minutes;
    memory_info_t memory;
    pthread_mutex_t lock;
} system_state_t;

void system_monitor_init(void);
void system_monitor_get_state(system_state_t *state);
void system_monitor_start(void);

#endif