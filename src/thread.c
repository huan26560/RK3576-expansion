/*
 * threads.c - 线程调度中心
 * 职责：创建线程，调用monitor接口，发布MQTT
 */

#include <stdio.h>
#include <stdint.h> // ✅ 添加这行
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include "threads.h"
#include "mqtt_client.h"
#include "hal_gpio.h"
#include "hal_system.h"
#include "system_monitor.h"
#include "hal_dht11.h"
#include "network_monitor.h"
#include "menu.h" // ✅ 添加这行，包含 event_t 和 EV_* 定义
#include <time.h>

/* ==================== 线程实现 ==================== */

void *mqtt_thread(void *arg)
{
    (void)arg;
    printf("[MQTT] Thread started\n");
    while (1)
    {
        mqtt_client_loop(10);
    }
    return NULL;
}

void *sensor_thread(void *arg)
{
    (void)arg;
    printf("[SENSOR] Thread started\n");
    while (1)
    {
        float temp, hum;
        if (hal_dht11_read(&temp, &hum) == 0)
        {
            char msg[64];
            snprintf(msg, sizeof(msg), "Temp:%.1f,Humi:%.1f", temp, hum);
            mqtt_publish("sensor/dht11", msg);
        }
        sleep(1);
    }
    return NULL;
}

void *system_publish_thread(void *arg)
{
    (void)arg;
    printf("[SYS-PUB] Thread started\n");
    system_monitor_start();

    while (1)
    {
        system_state_t state;
        system_monitor_get_state(&state);

        char msg[64];
        // ✅ 使用新的成员名
        snprintf(msg, sizeof(msg), "CPU:%d%%", state.cpu_total_usage);
        mqtt_publish("system/cpu", msg);

        snprintf(msg, sizeof(msg), "MEM:%d%%", state.memory.used_percent);
        mqtt_publish("system/mem", msg);

        snprintf(msg, sizeof(msg), "TEMP:%dC", state.gpu.temp_c); // 或者用其他温度值
        mqtt_publish("system/temp", msg);

        sleep(1);
    }
    return NULL;
}

void *network_publish_thread(void *arg)
{
    (void)arg;
    printf("[NET-PUB] Thread started\n");
    while (1)
    {
        network_state_t state;
        network_monitor_get_state(&state);

        if (state.wifi_connected)
        {
            char msg[128];
            snprintf(msg, sizeof(msg), "SSID:%s,IP:%s,SIG:%ddBm,INET:%d",
                     state.wifi_ssid, state.wifi_ip, state.wifi_signal, state.wifi_internet);
            mqtt_publish("network/wifi", msg);
        }

        char msg[32];
        snprintf(msg, sizeof(msg), "LAN:%d", state.lan_device_count);
        mqtt_publish("network/lan", msg);

        sleep(1);
    }
}

void *button_thread(void *arg)
{
    (void)arg;
    printf("[BUTTON] Thread started\n");
    hal_gpio_init();

    while (1)
    {
        int btn = hal_button_read();
        if (btn >= 0)
        {
            hal_led_set(0, 1);
            event_t ev;
            uint32_t dur = hal_button_wait_release(btn);

            switch (btn)
            {
            case 0:
                ev = EV_UP;
                break;
            case 1:
                ev = (dur > 200) ? EV_BACK : EV_ENTER;
                break;
            case 2:
                ev = EV_DOWN;
                break;
            default:
                continue;
            }

            menu_handle_event(ev);
            hal_led_set(0, 0);
        }
        usleep(10000);
    }
}


/* ==================== 线程管理 ==================== */

static void create_thread(pthread_t *t, void *(*f)(void *), const char *name)
{
    printf("[THREAD] Creating %s...\n", name);
    if (pthread_create(t, NULL, f, NULL) == 0)
    {
        pthread_detach(*t);
        printf("[THREAD] %s created\n", name);
    }
    else
    {
        fprintf(stderr, "[THREAD] FAILED to create %s\n", name);
    }
}


void threads_init(void)
{
    static int initialized = 0;
    if (initialized)
        return;

    printf("[THREAD] Initializing all threads...\n");

    network_monitor_init();

    pthread_t mqtt_t, sensor_t, sys_t, net_t, btn_t;
    create_thread(&mqtt_t, mqtt_thread, "MQTT");
    create_thread(&sensor_t, sensor_thread, "SENSOR");
    create_thread(&sys_t, system_publish_thread, "SYSTEM");
    create_thread(&net_t, network_publish_thread, "NETWORK");
    create_thread(&btn_t, button_thread, "BUTTON");

    initialized = 1;
    printf("[THREAD] All threads initialized\n");
}