#include <stdio.h>
#include "string.h"
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h> // 新增：原子操作
#include "hal_oled.h"
#include "hal_gpio.h"
#include "hal_system.h"
#include "mqtt_client.h"
#include "menu.h"
#include "thread.h"

static volatile int running = 1;

static atomic_int shutdown_mode = 0; // 0=正常退出，1=系统重启

void signal_handler(int sig)
{
    if (shutdown_mode != 0)
        return;
    // 统一视为程序退出（清屏+清理）
    printf("【Signal】捕获终止信号: %d\n", sig);
    shutdown_mode = 1; // 标记为需要退出动画+清理
    running = 0;
}
int main(void)
{
    printf("=== 哈吉米3 扩展板 MQTT UI 系统启动 ===\n");

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGTERM, &sa, NULL); // reboot/shutdown
    sigaction(SIGINT, &sa, NULL);  // Ctrl+C

    // 忽略网络断开导致的SIGPIPE
    signal(SIGPIPE, SIG_IGN);

    // 1. 资源初始化
    printf("初始化硬件...\n");

    hal_system_init();
    hal_gpio_init();
    hal_oled_init();
    hal_oled_clear();
    hal_oled_string(22, 25, "Starting up...");
    hal_oled_refresh();
    hal_led_all_off();
    threads_init();
    // 2. MQTT初始化
    printf("初始化MQTT...\n");
    mqtt_client_init("localhost", 1883);

    // 4. 菜单初始化
    menu_init();

    // 主循环
    while (running)
    {
        menu_render();
        hal_oled_refresh();
        usleep(16666); // 60Hz
    }

    // == 统一清理逻辑（无论何种退出方式）==
    hal_oled_clear();
    hal_oled_string(20, 25, "Shutting down...");
    hal_oled_refresh();
    usleep(100000); // 显示0.5秒

    // 永远清屏
    hal_oled_clear();
    hal_oled_refresh();
    usleep(100000); // 等待物理刷新完成

    // 清理资源（只在程序正常退出时执行）
    if (shutdown_mode == 1 || shutdown_mode == 0) // 1: 信号退出, 0: running=0
    {
        printf("清理资源...\n");
        hal_led_all_off();
        usleep(50000); // 等待50ms确保LED物理关闭
        mqtt_client_cleanup();
        hal_gpio_cleanup();
        hal_system_cleanup();
        printf("系统已安全退出\n");
    }

    return 0;
}