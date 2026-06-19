#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "hal_dht11.h"

#define DEV_NAME "/dev/dht11"

// ---------- 平滑度由这两个常数决定（0<值<1, 越小越平滑）----------
#define KALMAN_GAIN_TEMP 0.08f // 温度增益，例如0.08表示每次只相信新值8%
#define KALMAN_GAIN_HUM 0.08f  // 湿度增益

// 缓存上次有效数据（滤波值）
static float last_temp = 0.0f;
static float last_hum = 0.0f;
static int has_valid = 0; // 是否已获得过有效值

int hal_dht11_read(float *temp, float *hum)
{
    static int first_run = 1;
    static int dev_exists = 1;

    // 首次运行检查设备
    if (first_run)
    {
        first_run = 0;
        if (access(DEV_NAME, F_OK) != 0)
        {
            fprintf(stderr, "[DHT11] Device %s not exist!\n", DEV_NAME);
            dev_exists = 0;
            *temp = last_temp;
            *hum = last_hum;
            return 0;
        }
        if (access(DEV_NAME, R_OK | W_OK) != 0)
        {
            fprintf(stderr, "[DHT11] No permission to access %s\n", DEV_NAME);
            dev_exists = 0;
            *temp = last_temp;
            *hum = last_hum;
            return 0;
        }
    }

    if (!dev_exists)
    {
        *temp = last_temp;
        *hum = last_hum;
        return 0;
    }

    int fd = open(DEV_NAME, O_RDWR);
    if (fd < 0)
    {
        *temp = last_temp;
        *hum = last_hum;
        return 0;
    }

    unsigned char data[4] = {0};
    int ret = read(fd, &data, sizeof(data));
    close(fd);

    if (ret > 0)
    {
        // 原始测量值（你的校准公式）
        float meas_temp = data[2] + data[3] * 0.1f - 3.0f;
        float meas_hum = data[0] + data[1] * 0.1f + 7.0f;

        // 合法范围过滤
        if (meas_temp >= 0 && meas_temp <= 50 && meas_hum >= 20 && meas_hum <= 90)
        {
            if (!has_valid)
            {
                // 第一次直接使用测量值
                last_temp = meas_temp;
                last_hum = meas_hum;
                has_valid = 1;
            }
            else
            {
                // 稳态卡尔曼更新（固定增益，绝对平滑）
                last_temp = last_temp + KALMAN_GAIN_TEMP * (meas_temp - last_temp);
                last_hum = last_hum + KALMAN_GAIN_HUM * (meas_hum - last_hum);
            }
        }
        // 测量值不合法 → 保持上次值不变
    }
    // 读取失败 → 保持上次值不变

    *temp = last_temp;
    *hum = last_hum;
    return 0;
}