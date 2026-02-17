#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <gpiod.h>
#include <errno.h>
#include <linux/input.h>
#include "hal_gpio.h"

static int initialized = 0;
static int btn_fd = -1;
static struct gpiod_chip *gpio_chip = NULL;
static struct gpiod_line *led_lines[3] = {NULL, NULL, NULL};  // 三个LED
static struct gpiod_line *beep_line = NULL;                   // 蜂鸣器

// 初始化（一次性打开所有设备）
int hal_gpio_init(void)
{
    if (initialized) {
        printf("GPIO: 已初始化，跳过\n");
        return 0;
    }

    // 打开GPIO芯片（所有line共享同一个chip）
    gpio_chip = gpiod_chip_open_by_name("gpiochip6");
    if (!gpio_chip) {
        printf("GPIO: 无法打开 gpiochip6\n");
        return -1;
    }

    // 初始化三个LED
    for (int i = 0; i < 3; i++) {
        led_lines[i] = gpiod_chip_get_line(gpio_chip, i);  // line 0,1,2
        if (!led_lines[i]) {
            printf("GPIO: 无法获取 LED line %d\n", i);
            goto ERROR;
        }
        
        if (gpiod_line_request_output(led_lines[i], "expansion_ui", 0) < 0) {
            printf("GPIO: 无法请求 LED%d 为输出\n", i);
            goto ERROR;
        }
    }

    // 初始化蜂鸣器（line 6）
    beep_line = gpiod_chip_get_line(gpio_chip, BEEP_LINE);
    if (!beep_line) {
        printf("GPIO: 无法获取蜂鸣器 line %d\n", BEEP_LINE);
        goto ERROR;
    }
    
    if (gpiod_line_request_output(beep_line, "beep", 0) < 0) {
        printf("GPIO: 无法请求蜂鸣器为输出\n");
        goto ERROR;
    }

    // 打开按钮输入设备
    btn_fd = open(BUTTON_DEV, O_RDONLY | O_NONBLOCK);
    if (btn_fd < 0) {
        perror("GPIO: 打开按键设备失败");
        goto ERROR;
    }

    initialized = 1;
    printf("GPIO: 初始化成功 (LED:0-2, Beep:%d, Buttons:%s)\n", BEEP_LINE, BUTTON_DEV);
    return 0;

ERROR:
    // 错误时清理已分配的资源
    for (int i = 0; i < 3; i++) {
        if (led_lines[i]) {
            gpiod_line_release(led_lines[i]);
            led_lines[i] = NULL;
        }
    }
    if (beep_line) {
        gpiod_line_release(beep_line);
        beep_line = NULL;
    }
    if (gpio_chip) {
        gpiod_chip_close(gpio_chip);
        gpio_chip = NULL;
    }
    return -1;
}

// 读取按钮
int hal_button_read(void)
{
    if (btn_fd < 0) return -1;
    
    struct input_event ev;
    if (read(btn_fd, &ev, sizeof(ev)) == sizeof(ev)) {
        if (ev.type == EV_KEY && ev.value == 1) {
            if (ev.code == KEYCODE_BTN0) return 0;
            if (ev.code == KEYCODE_BTN1) return 1;
            if (ev.code == KEYCODE_BTN2) return 2;
        }
    }
    return -1;
}

// 等待按键释放
int hal_button_wait_release(int btn)
{
    if (btn_fd < 0) return 0;
    
    int target_code = (btn == 0) ? KEYCODE_BTN0 : (btn == 1) ? KEYCODE_BTN1 : KEYCODE_BTN2;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint32_t start = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    int timeout = 0;
    while (timeout < 500) {
        struct input_event ev;
        int n = read(btn_fd, &ev, sizeof(ev));
        if (n == sizeof(ev) && ev.type == EV_KEY && ev.code == target_code && ev.value == 0) {
            clock_gettime(CLOCK_MONOTONIC, &ts);
            uint32_t now = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            return now - start;
        }
        usleep(10000);
        timeout++;
    }
    return 5000;
}

// LED控制（三通道）
void hal_led_set(int led_id, int on)
{
    if (led_id >= 0 && led_id < 3 && led_lines[led_id]) {
        // on=1 → 亮 → 输出0
        // on=0 → 灭 → 输出1
        gpiod_line_set_value(led_lines[led_id], on ? 0 : 1);
    }
}

// 关闭所有LED（真正关掉：全部输出1）
void hal_led_all_off(void)
{
    for (int i = 0; i < 3; i++) {
        hal_led_set(i, 0); // 这里传0，上面会自动设为1（灭）
    }
}
// 蜂鸣器控制
void hal_beep(int ms)
{
    if (beep_line) {
        gpiod_line_set_value(beep_line, 1);
        usleep(ms * 1000);
        gpiod_line_set_value(beep_line, 0);
    }
}

// 清理（修复：释放所有资源）
void hal_gpio_cleanup(void)
{
    if (!initialized) return;

    if (btn_fd >= 0) {
        close(btn_fd);
        btn_fd = -1;
    }

    for (int i = 0; i < 3; i++) {
        if (led_lines[i]) {
            gpiod_line_release(led_lines[i]);
            led_lines[i] = NULL;
        }
    }

    if (beep_line) {
        gpiod_line_release(beep_line);
        beep_line = NULL;
    }

    if (gpio_chip) {
        gpiod_chip_close(gpio_chip);
        gpio_chip = NULL;
    }

    initialized = 0;
    printf("GPIO: 已清理\n");
}