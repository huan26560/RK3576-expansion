/* hal_echo.c - HC-SR04 优化版 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <gpiod.h>
#include "hal_echo.h"

#define GPIO_CHIP_TRIG      "/dev/gpiochip0"   /* 修正为 gpiochip0 */
#define GPIO_OFFSET_TRIG    19
#define GPIO_OFFSET_ECHO    21

#define TRIG_PULSE_US       10
#define TIMEOUT_US          25000              /* 25ms 超时 (≈4.3米) */
#define SOUND_SPEED_CM_PER_US  0.0343f

/* 优化: 提高滤波增益，加快响应速度 */
#define KALMAN_GAIN_DIST    0.45f              /* 从0.12提高到0.45 */

static float last_dist = 0.0f;
static int has_valid = 0;
static struct gpiod_line *trig_line = NULL;
static struct gpiod_line *echo_line = NULL;
static struct gpiod_chip *g_chip = NULL;       /* 保存chip句柄 */
static int init_done = 0;

static long long get_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

/* 优化: 去掉usleep，纯忙等待 */
static int wait_for_level(int target_level, long long timeout_us)
{
    long long start = get_time_us();
    int val;
    while (1) {
        val = gpiod_line_get_value(echo_line);
        if (val < 0) return -1;
        if (val == target_level) return 0;
        if ((get_time_us() - start) > timeout_us) return -1;
        /* 不再 usleep，避免错过边沿 */
    }
}

static int hcsr04_init(void)
{
    if (init_done) return 0;
    g_chip = gpiod_chip_open(GPIO_CHIP_TRIG);
    if (!g_chip) {
        fprintf(stderr, "[HCSR04] Failed to open %s\n", GPIO_CHIP_TRIG);
        return -1;
    }
    trig_line = gpiod_chip_get_line(g_chip, GPIO_OFFSET_TRIG);
    if (!trig_line) {
        fprintf(stderr, "[HCSR04] Failed to get trig line %d\n", GPIO_OFFSET_TRIG);
        gpiod_chip_close(g_chip);
        return -1;
    }
    if (gpiod_line_request_output(trig_line, "hcsr04_trig", 0) < 0) {
        fprintf(stderr, "[HCSR04] Failed to request trig as output\n");
        gpiod_chip_close(g_chip);
        return -1;
    }
    gpiod_line_set_value(trig_line, 0);

    echo_line = gpiod_chip_get_line(g_chip, GPIO_OFFSET_ECHO);
    if (!echo_line) {
        fprintf(stderr, "[HCSR04] Failed to get echo line %d\n", GPIO_OFFSET_ECHO);
        gpiod_line_release(trig_line);
        gpiod_chip_close(g_chip);
        return -1;
    }
    if (gpiod_line_request_input(echo_line, "hcsr04_echo") < 0) {
        fprintf(stderr, "[HCSR04] Failed to request echo as input\n");
        gpiod_line_release(trig_line);
        gpiod_chip_close(g_chip);
        return -1;
    }
    init_done = 1;
    return 0;
}

static void send_trigger(void)
{
    gpiod_line_set_value(trig_line, 1);
    usleep(TRIG_PULSE_US);
    gpiod_line_set_value(trig_line, 0);
}

int hal_hcsr04_read_distance(float *dist)
{
    long long start_us, end_us, echo_duration_us;
    float raw_dist;

    if (hcsr04_init() != 0) {
        if (!has_valid) { *dist = 0.0f; return -1; }
        else { *dist = last_dist; return 0; }
    }

    /* 确保 Echo 为低电平后再触发，避免残高导致等待 */
    wait_for_level(0, 5000);   // 最多等5ms

    send_trigger();

    /* 等待 Echo 上升沿 */
    if (wait_for_level(1, TIMEOUT_US) != 0) {
        if (!has_valid) { *dist = 0.0f; return -1; }
        else { *dist = last_dist; return 0; }
    }
    start_us = get_time_us();

    /* 等待 Echo 下降沿 */
    if (wait_for_level(0, TIMEOUT_US) != 0) {
        if (!has_valid) { *dist = 0.0f; return -1; }
        else { *dist = last_dist; return 0; }
    }
    end_us = get_time_us();

    echo_duration_us = end_us - start_us;
    raw_dist = echo_duration_us * SOUND_SPEED_CM_PER_US / 2.0f;

    if (raw_dist >= 2.0f && raw_dist <= 400.0f) {
        if (!has_valid) {
            last_dist = raw_dist;
            has_valid = 1;
        } else {
            last_dist = last_dist + KALMAN_GAIN_DIST * (raw_dist - last_dist);
        }
    } else {
        if (!has_valid) { *dist = 0.0f; return -1; }
    }
    *dist = last_dist;
    return 0;
}

void hal_hcsr04_cleanup(void)
{
    if (trig_line) { gpiod_line_release(trig_line); trig_line = NULL; }
    if (echo_line) { gpiod_line_release(echo_line); echo_line = NULL; }
    if (g_chip) { gpiod_chip_close(g_chip); g_chip = NULL; }
    init_done = 0;
}