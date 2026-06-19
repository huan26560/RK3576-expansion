#include "system_monitor.h"
#include "hal_system.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <pthread.h>

static system_state_t g_system_state = {0};
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;

static int read_cpu_freq(int cpu_id)
{
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu_id);
    
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    
    int freq_khz = 0;
    fscanf(fp, "%d", &freq_khz);
    fclose(fp);
    
    return freq_khz / 1000;
}

static core_type_t detect_core_type(int cpu_id)
{
    return (cpu_id < 4) ? CORE_LITTLE : CORE_BIG;
}

static void update_cpu_info(system_state_t *state)
{
    DIR *dir = opendir("/sys/devices/system/cpu");
    if (!dir) return;
    
    // ✅ 先读取总CPU使用率
    state->cpu_total_usage = hal_cpu_usage();
    
    int core_count = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL && core_count < MAX_CPU_CORES)
    {
        if (strncmp(entry->d_name, "cpu", 3) == 0 && 
            entry->d_name[3] >= '0' && entry->d_name[3] <= '9')
        {
            int id = atoi(entry->d_name + 3);
            
            snprintf(state->cpu_cores[core_count].name, 8, "CPU%d", id);
            state->cpu_cores[core_count].type = detect_core_type(id);
            state->cpu_cores[core_count].freq_mhz = read_cpu_freq(id);
            state->cpu_cores[core_count].usage_percent = state->cpu_total_usage; // ✅ 简化处理
            state->cpu_cores[core_count].temp_c = (int)hal_cpu_temp();
            core_count++;
        }
    }
    closedir(dir);
    
    state->cpu_core_count = core_count;
}

static void update_gpu_info(system_state_t *state)
{
    // 读取 GPU 频率
    FILE *fp = fopen("/sys/class/devfreq/27800000.gpu/cur_freq", "r");
    if (fp) {
        int freq_hz = 0;
        fscanf(fp, "%d", &freq_hz);
        fclose(fp);
        state->gpu.freq_mhz = freq_hz / 1000000;
    } else {
        state->gpu.freq_mhz = 0;
    }
    
    // ✅ 读取 GPU 负载和使用率
    FILE *load_fp = fopen("/sys/class/devfreq/27800000.gpu/load", "r");
    if (load_fp) {
        int usage = 0, freq = 0;
        // 格式: 100@950000000Hz
        fscanf(load_fp, "%d@%dHz", &usage, &freq);
        fclose(load_fp);
        
        state->gpu.usage_percent = usage;
        // 可以从频率确认，如果读取的频率和load文件不一致，以load文件为准
        if (freq > 0) {
            state->gpu.freq_mhz = freq / 1000000;
        }
    } else {
        // 如果load文件不存在，保持模拟数据
        state->gpu.usage_percent = 45;
    }
    
    state->gpu.temp_c = (int)hal_cpu_temp();
}
static void update_npu_info(system_state_t *state)
{
    // 1. 读取 NPU 频率（路径：/sys/kernel/debug/rknpu/freq）
    FILE *fp_freq = fopen("/sys/kernel/debug/rknpu/freq", "r");
    if (fp_freq) {
        long long freq_hz = 0;  // 避免950000000溢出int
        fscanf(fp_freq, "%lld", &freq_hz);
        fclose(fp_freq);
        state->npu.freq_mhz = (int)(freq_hz / 1000000);
    } else {
        state->npu.freq_mhz = 0;
        printf("Warning: NPU freq file open failed\n");
    }
    
    // 2. 读取 NPU 多核心负载（严格匹配hexdump格式）
    FILE *fp_load = fopen("/sys/kernel/debug/rknpu/load", "r");
    if (fp_load) {
        int core0 = 0, core1 = 0;
        // 格式串完全匹配：NPU load:  Core0:  %d%%, Core1:  %d%%,
        int parse_ret = fscanf(fp_load, "NPU load:  Core0:  %d%%, Core1:  %d%%,", &core0, &core1);
        
        // 解析失败时赋默认值（避免垃圾值）
        if (parse_ret != 2) {
            core0 = 0;
            core1 = 0;
            printf("Warning: NPU load parse failed (ret=%d)\n", parse_ret);
        }
        
        fclose(fp_load);
        
        // 填充多核心数据
        state->npu.core_usage[0] = core0;  // Core0使用率
        state->npu.core_usage[1] = core1;  // Core1使用率
        state->npu.usage_percent = (core0 + core1) / 2;  // 总使用率（平均值）
    } else {
        // 文件打开失败时赋默认值
        state->npu.core_usage[0] = 0;
        state->npu.core_usage[1] = 0;
        state->npu.usage_percent = 0;
        printf("Warning: NPU load file open failed\n");
    }
    
    // 3. 读取温度
    state->npu.temp_c = (int)hal_cpu_temp();
}
static void update_memory_info(system_state_t *state)
{
    FILE *fp = fopen("/proc/meminfo", "r");
    if (!fp) return;
    
    long total = 0, free = 0, avail = 0, buffers = 0, cached = 0;
    char line[128];
    
    while (fgets(line, sizeof(line), fp)) {
        sscanf(line, "MemTotal: %ld kB", &total);
        sscanf(line, "MemFree: %ld kB", &free);
        sscanf(line, "MemAvailable: %ld kB", &avail);
        sscanf(line, "Buffers: %ld kB", &buffers);
        sscanf(line, "Cached: %ld kB", &cached);
    }
    fclose(fp);
    
    state->memory.total_mb = total / 1024;
    state->memory.free_mb = free / 1024;
    state->memory.available_mb = avail / 1024;
    state->memory.buffers_mb = buffers / 1024;
    state->memory.cached_mb = cached / 1024;
    state->memory.used_percent = (total > 0) ? ((total - avail) * 100 / total) : 0;
}

static void* system_monitor_thread(void *arg)
{
    (void)arg;
    while (1) {
        system_state_t temp_state;
        memset(&temp_state, 0, sizeof(temp_state));
        
        update_cpu_info(&temp_state);
        update_gpu_info(&temp_state);
        update_npu_info(&temp_state);
        update_memory_info(&temp_state);
        temp_state.uptime_minutes = hal_uptime_minutes();
        
        pthread_mutex_lock(&state_lock);
        memcpy(&g_system_state, &temp_state, sizeof(g_system_state));
        pthread_mutex_unlock(&state_lock);
        
        usleep(500000);
    }
    return NULL;
}

void system_monitor_init(void)
{
    static int initialized = 0;
    if (initialized) return;
    
    memset(&g_system_state, 0, sizeof(g_system_state));
    pthread_mutex_init(&g_system_state.lock, NULL);
    
    update_cpu_info(&g_system_state);
    update_gpu_info(&g_system_state);
    update_npu_info(&g_system_state);
    update_memory_info(&g_system_state);
    g_system_state.uptime_minutes = hal_uptime_minutes();
    
    initialized = 1;
}

void system_monitor_get_state(system_state_t *state)
{
    if (!state) return;
    pthread_mutex_lock(&state_lock);
    memcpy(state, &g_system_state, sizeof(system_state_t));
    pthread_mutex_unlock(&state_lock);
}

void system_monitor_start(void)
{
    static pthread_t thread;
    static int started = 0;
    if (started) return;
    
    pthread_create(&thread, NULL, system_monitor_thread, NULL);
    pthread_detach(thread);
    started = 1;
}