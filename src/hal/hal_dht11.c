#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "hal_dht11.h"

#define DEV_NAME "/dev/dht11"
// 仅加1次重试（避免过度修改）+ 缓存
#define READ_RETRY_CNT 1
#define RETRY_DELAY_MS 50

// 缓存上次有效数据（仅用于兜底）
static float last_temp = 0.0f;
static float last_hum = 0.0f;
static int has_valid = 0;

int hal_dht11_read(float *temp, float *hum)
{
    static int first_run = 1;
    static int dev_exists = 1; // 标记设备是否存在

    // 首次运行：检查设备文件是否存在，只检查1次
    if (first_run)
    {
        first_run = 0;
        // 检查设备文件是否存在（用access替代open，减少资源占用）
        if (access(DEV_NAME, F_OK) != 0)
        {
            fprintf(stderr, "[DHT11] Device %s not exist!\n", DEV_NAME);
            dev_exists = 0;
            *temp = last_temp;  // 👈 改这里：返回缓存，不写死25
            *hum = last_hum;
            return 0;
        }
        // 检查权限
        if (access(DEV_NAME, R_OK | W_OK) != 0)
        {
            fprintf(stderr, "[DHT11] No permission to access %s\n", DEV_NAME);
            dev_exists = 0;
            *temp = last_temp;  // 👈 改这里：返回缓存
            *hum = last_hum;
            return 0;
        }
    }

    // 设备不存在：直接返回默认值，不执行open/read
    if (!dev_exists)
    {
        *temp = last_temp;    // 👈 改这里：返回缓存
        *hum = last_hum;
        return 0;
    }

    // 原有逻辑（保留，但只执行1次，不重试）
    int fd = open(DEV_NAME, O_RDWR);
    if (fd < 0)
    {
        *temp = last_temp;    // 👈 改这里：返回缓存
        *hum = last_hum;
        return 0;
    }

    unsigned char data[4] = {0};
    int ret = read(fd, &data, sizeof(data));
    close(fd);

    if (ret > 0)
    {
        // 读取成功：更新缓存
        float t = data[2] + data[3] * 0.1f-4.0f;
        float h = data[0] + data[1] * 0.1f;

        // 合法范围过滤
        if(t >= 0 && t <= 50 && h >= 20 && h <= 90)
        {
            last_temp = t;
            last_hum = h;
        }

        *temp = last_temp;
        *hum = last_hum;
        return 0;
    }

    // 读取失败：返回上一次缓存，不再返回25
    *temp = last_temp;    // 👈 改这里：返回缓存
    *hum = last_hum;
    return 0;
}