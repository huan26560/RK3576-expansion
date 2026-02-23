#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#include "hal_oled.h"
#include "hal_gpio.h"
#include "hal_system.h"
#include "mqtt_client.h"
#include "menu.h"
#include "thread.h"

// 定义常量
#define DISPLAY_REFRESH_RATE_HZ 60
#define DISPLAY_DELAY_US (1000000 / DISPLAY_REFRESH_RATE_HZ)
#define SHUTDOWN_DISPLAY_DURATION_US 500000
#define CLEANUP_DELAY_US 50000

static volatile int running = 1;
static atomic_int shutdown_mode = 0; // 0=正常退出，1=信号退出

/**
 * @brief 信号处理函数
 * @param sig 信号值
 */
void signal_handler(int sig)
{
    // 防止重复处理信号
    if (shutdown_mode != 0)
        return;

    // 统一视为程序退出（清屏+清理）
    printf("【Signal】捕获终止信号: %d\n", sig);
    shutdown_mode = 1; // 标记为信号退出
    running = 0;
}

/**
 * @brief 初始化信号处理
 */
static void init_signals(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    // 注册终止信号
    sigaction(SIGTERM, &sa, NULL); // reboot/shutdown
    sigaction(SIGINT, &sa, NULL);  // Ctrl+C

    // 忽略网络断开导致的SIGPIPE
    signal(SIGPIPE, SIG_IGN);
}

/**
 * @brief 显示启动界面
 */
static void show_startup_screen(void)
{
    hal_oled_clear();
    hal_oled_string(22, 25, "Starting up...");
    hal_oled_refresh();
}

/**
 * @brief 显示关机界面
 */
static void show_shutdown_screen(void)
{
    hal_oled_clear();
    hal_oled_string(20, 25, "Shutting down...");
    hal_oled_refresh();
    usleep(SHUTDOWN_DISPLAY_DURATION_US);

    // 清屏
    hal_oled_clear();
    hal_oled_refresh();
    usleep(100000);
}

/**
 * @brief 清理系统资源
 */
static void cleanup_resources(void)
{
    printf("清理资源...\n");

    // 关闭所有LED
    hal_led_all_off();
    usleep(CLEANUP_DELAY_US);

    // 清理其他资源
    mqtt_client_cleanup();
    hal_gpio_cleanup();
    hal_system_cleanup();

    printf("系统已安全退出\n");
}

int main(void)
{
    printf("=== 哈吉米3 扩展板 MQTT UI 系统启动 ===\n");

    // 1. 初始化信号处理
    init_signals();

    // 2. 初始化硬件
    printf("初始化硬件...\n");
    hal_system_init();
    hal_gpio_init();
    hal_oled_init();
    show_startup_screen();
    hal_led_all_off();

    // 3. 初始化线程
    threads_init();

    // 4. 初始化MQTT
    printf("初始化MQTT...\n");
    mqtt_client_init("localhost", 1883);

    // 5. 初始化菜单
    menu_init();

    // 6. 主循环
    printf("进入主循环...\n");
    while (running)
    {
        menu_render();
        usleep(DISPLAY_DELAY_US); // 60Hz刷新率
    }

    // 7. 退出处理
    printf("正在退出系统...\n");
    show_shutdown_screen();

    // 8. 清理资源
    cleanup_resources();

    return 0;
}