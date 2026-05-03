#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include "hal_dht11.h"

#define DEV_NAME "/dev/dht11"
// 仅加1次重试（避免过度修改）+ 缓存
#define READ_RETRY_CNT 1
#define RETRY_DELAY_MS 50

// 卡尔曼滤波参数（可根据实际传感器噪声调整）
#define KF_Q_TEMP  0.01f   // 温度过程噪声
#define KF_R_TEMP  5.0f     // 温度测量噪声
#define KF_Q_HUM   0.01f    // 湿度过程噪声
#define KF_R_HUM   3.0f     // 湿度测量噪声

// 缓存上次有效数据（滤波后的值）
static float last_temp = 0.0f;
static float last_hum = 0.0f;
static int has_valid = 0;   // 是否已有有效滤波估计

// 卡尔曼滤波器状态
static float kf_x_temp = 25.0f;   // 初始猜测
static float kf_P_temp = 100.0f;  // 初始方差（大一些，快速收敛）
static float kf_x_hum  = 50.0f;
static float kf_P_hum  = 100.0f;
static int kf_initialized = 0;    // 是否已用测量值初始化

int hal_dht11_read(float *temp, float *hum)
{
    static int first_run = 1;
    static int dev_exists = 1; // 标记设备是否存在

    // 首次运行：检查设备文件是否存在，只检查1次
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

    // 设备不存在：返回缓存值
    if (!dev_exists)
    {
        *temp = last_temp;
        *hum = last_hum;
        return 0;
    }

    // 打开设备
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
        // 原始测量值（带校准偏移）
        float meas_temp = data[2] + data[3] * 0.1f - 3.0f;
        float meas_hum  = data[0] + data[1] * 0.1f;

        // 合法范围过滤
        if (meas_temp >= 0 && meas_temp <= 50 && meas_hum >= 20 && meas_hum <= 90)
        {
            // --- 卡尔曼滤波更新（温度） ---
            if (!kf_initialized) {
                // 首次有效测量直接作为初始状态
                kf_x_temp = meas_temp;
                kf_P_temp = 1.0f;    // 初始方差可以适当设置
                kf_x_hum  = meas_hum;
                kf_P_hum  = 1.0f;
                kf_initialized = 1;
            } else {
                // 预测步骤：增加过程噪声
                kf_P_temp += KF_Q_TEMP;
                kf_P_hum  += KF_Q_HUM;

                // 更新步骤（温度）
                float K_temp = kf_P_temp / (kf_P_temp + KF_R_TEMP);
                kf_x_temp = kf_x_temp + K_temp * (meas_temp - kf_x_temp);
                kf_P_temp = (1.0f - K_temp) * kf_P_temp;  // P更新

                // 更新步骤（湿度）
                float K_hum = kf_P_hum / (kf_P_hum + KF_R_HUM);
                kf_x_hum = kf_x_hum + K_hum * (meas_hum - kf_x_hum);
                kf_P_hum = (1.0f - K_hum) * kf_P_hum;
            }

            // 用滤波值更新缓存
            last_temp = kf_x_temp;
            last_hum  = kf_x_hum;
            has_valid = 1;
        }
        else
        {
            // 测量值不合法：仅进行预测（增加过程噪声），状态不变
            if (kf_initialized) {
                kf_P_temp += KF_Q_TEMP;
                kf_P_hum  += KF_Q_HUM;
                // 防止长期无有效值时方差无限增大（可选）
                if (kf_P_temp > 100.0f) kf_P_temp = 100.0f;
                if (kf_P_hum  > 100.0f) kf_P_hum  = 100.0f;
            }
            // 缓存值保持上次有效滤波值（last_temp/last_hum 不变）
        }

        // 输出值始终从滤波缓存中取
        *temp = last_temp;
        *hum  = last_hum;
        return 0;
    }

    // 读取失败：返回上一次缓存
    *temp = last_temp;
    *hum = last_hum;
    return 0;
}