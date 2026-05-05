#ifndef HAL_ECHO_H
#define HAL_ECHO_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 读取 HC-SR04 超声波模块测量的距离
 * @param dist 输出参数，距离值（厘米）
 * @return 0 表示成功，-1 表示失败
 */
int hal_hcsr04_read_distance(float *dist);

/**
 * 释放 GPIO 资源（程序退出时可选调用）
 */
void hal_hcsr04_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif