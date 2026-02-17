/*
 * hal_system.c - 硬件抽象层
 * 直接操作/proc和/sys，提供基础数据
 */

#include "hal_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <time.h>
// 静态文件描述符
static int cpu_fd = -1, temp_fd = -1, mem_fd = -1;

void hal_system_init(void)
{
    cpu_fd = open("/proc/stat", O_RDONLY);
    temp_fd = open("/sys/class/thermal/thermal_zone0/temp", O_RDONLY);
    mem_fd = open("/proc/meminfo", O_RDONLY);
}

void hal_system_cleanup(void)
{
    if (cpu_fd >= 0) close(cpu_fd);
    if (temp_fd >= 0) close(temp_fd);
    if (mem_fd >= 0) close(mem_fd);
}

// CPU使用率
int hal_cpu_usage(void)
{
    if (cpu_fd < 0) return -1;
    lseek(cpu_fd, 0, SEEK_SET);
    
    char buf[256];
    int n = read(cpu_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return -1;
    buf[n] = '\0';
    
    long user, nice, sys, idle;
    sscanf(buf, "cpu %ld %ld %ld %ld", &user, &nice, &sys, &idle);
    
    static long prev_total = 0, prev_idle = 0;
    long total = user + nice + sys + idle;
    long diff_total = total - prev_total;
    long diff_idle = idle - prev_idle;
    
    prev_total = total;
    prev_idle = idle;
    
    return (diff_total - diff_idle) * 100 / diff_total;
}

// CPU温度
float hal_cpu_temp(void)
{
    if (temp_fd < 0) return -1.0;
    lseek(temp_fd, 0, SEEK_SET);
    
    char buf[32];
    int n = read(temp_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return -1.0;
    
    return atoi(buf) / 1000.0;
}

// 内存使用率
int hal_mem_usage(void)
{
    if (mem_fd < 0) return -1;
    lseek(mem_fd, 0, SEEK_SET);
    
    char buf[256];
    int n = read(mem_fd, buf, sizeof(buf) - 1);
    if (n <= 0) return -1;
    
    long total, free;
    sscanf(buf, "MemTotal: %ld kB\nMemFree: %ld kB", &total, &free);
    
    return (total - free) * 100 / total;
}

// WiFi信号强度（底层实现）
int hal_wifi_signal(void)
{
    char cmd[] = "iw dev wlan0 link 2>&1 | grep 'signal:' | awk '{print $2}'";
    char result[16] = {0};
    
    FILE *fp = popen(cmd, "r");
    if (!fp) return -999;
    
    fgets(result, sizeof(result), fp);
    pclose(fp);
    
    return atoi(result);
}

// 网络连通性检测（底层）
int hal_net_connected(void)
{
    return (system("ping -c 1 8.8.8.8 -W 2 > /dev/null 2>&1") == 0);
}

// 统一时间函数
unsigned long millis(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
// 读取系统运行时间（分钟）
int hal_uptime_minutes(void)
{
    FILE *fp = fopen("/proc/uptime", "r");
    if (!fp) return 0;
    
    double uptime_seconds;
    fscanf(fp, "%lf", &uptime_seconds);
    fclose(fp);
    
    return (int)(uptime_seconds / 60);
}