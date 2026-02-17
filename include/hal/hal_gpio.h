#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <linux/input.h>  // 必须包含

// LED 配置（gpiod）
#define LED_CHIP    "gpiochip6"
#define LED_LINE    0
#define LED_LINE_0      0  // LED 0（红色）
#define LED_LINE_1      1  // LED 1（绿色）
#define LED_LINE_2      2  // LED 2（蓝色）
#define BEEP_LINE       6  // 蜂鸣器
// 按键设备（input 子系统）
#define BUTTON_DEV  "/dev/input/event10"

// 实际按键码（来自 evtest）
#define KEYCODE_BTN0  11
#define KEYCODE_BTN1  2
#define KEYCODE_BTN2  3

// 函数声明
void hal_beep(int ms);
int hal_gpio_init(void);
int hal_button_read(void);
int hal_button_wait_release(int btn);
void hal_gpio_cleanup(void);
void hal_beep(int ms);
void hal_led_set(int led_id, int on);  // led_id: 0/1/2
void hal_led_all_off(void);            // 关闭所有LED

#endif