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
    // 保留你原有逻辑：每次都open/close，用O_RDWR
    int fd = open(DEV_NAME, O_RDWR);
    if (fd < 0) { // 新增：判断fd是否打开成功（避免读无效fd）
        fprintf(stderr, "[DHT11] Open fail\n");
        // 有缓存则返回缓存，无缓存返回错误
        if (has_valid) {
            *temp = last_temp;
            *hum = last_hum;
            close(fd); // 即使open失败也要close（防御性）
            return 0;
        }
        close(fd);
        return -1;
    }

    unsigned char data[4] = {0};
    int ret = read(fd, &data, sizeof(data));
    close(fd);

    // 第一步：按你原有逻辑判断是否读取成功
    if (ret > 0) {                                   
        *temp = data[2] + data[3] * 0.1; 
        *hum = data[0] + data[1] * 0.1;  
        // 新增：缓存有效数据
        last_temp = *temp;
        last_hum = *hum;
        has_valid = 1;
        return 0;
    }

    // 第二步：读取失败 → 重试1次（仅1次，不改动太多）
    if (READ_RETRY_CNT > 0) {
        usleep(RETRY_DELAY_MS * 1000);
        fd = open(DEV_NAME, O_RDWR);
        if (fd >= 0) {
            ret = read(fd, &data, sizeof(data));
            close(fd);
            if (ret > 0) {
                *temp = data[2] + data[3] * 0.1;
                *hum = data[0] + data[1] * 0.1;
                last_temp = *temp;
                last_hum = *hum;
                has_valid = 1;
                return 0;
            }
        }
    }

    // 第三步：重试也失败 → 有缓存就返回缓存（不报错），无缓存才返回-1
    if (has_valid) {
        *temp = last_temp;
        *hum = last_hum;
        fprintf(stderr, "[DHT11] Read fail, use cache: %.1fC %.1f%%\n", *temp, *hum);
        return 0; // 返回0，让UI认为成功，不显示Error
    }

    // 仅首次启动无缓存时才返回错误
    return -1;
}