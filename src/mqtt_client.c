#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mosquitto.h>
#include <time.h>
#include <unistd.h>
#include "mqtt_client.h"
#include "hal_gpio.h"
#include <unistd.h> // 用于 usleep

static struct mosquitto *mosq = NULL;
static char broker_host[64] = "192.168.31.110";
static int broker_port = 1883;
static int mqtt_initialized = 0;

// 重连状态
static int mqtt_connected = 0;
static uint64_t last_reconnect_attempt = 0;
#define RECONNECT_INTERVAL_MS 1000

extern struct mosquitto *mosq;
extern void dashboard_update_remote(const char *topic, const char *payload);

static uint64_t get_time_ms(void);

int mqtt_is_connected(void)
{
    return (mosq && mqtt_connected);
}

void mqtt_on_connect(struct mosquitto *mosq, void *userdata, int rc)
{
    if (rc == 0)
    {
        mqtt_connected = 1;
        printf("MQTT: 连接成功\n");

        // 重连后重新订阅（mosquitto不自动恢复订阅）
        mosquitto_subscribe(mosq, NULL, "expansion/led", 0);
        mosquitto_subscribe(mosq, NULL, "expansion/beep", 0);
        mosquitto_subscribe(mosq, NULL, "expansion/led_rgb", 0);

        mosquitto_subscribe(mosq, NULL, "server/cpu", 0);
        mosquitto_subscribe(mosq, NULL, "server/mem", 0);
        mosquitto_subscribe(mosq, NULL, "server/temp", 0);
        mosquitto_subscribe(mosq, NULL, "server/net", 0);
        mosquitto_subscribe(mosq, NULL, "server/uptime", 0); // **新增**

        // 新增：香橙派5Plus订阅
        mosquitto_subscribe(mosq, NULL, "device/orangepi5plus/cpu", 0);
        mosquitto_subscribe(mosq, NULL, "device/orangepi5plus/mem", 0);
        mosquitto_subscribe(mosq, NULL, "device/orangepi5plus/temp", 0);
        mosquitto_subscribe(mosq, NULL, "device/orangepi5plus/net", 0);
        mosquitto_subscribe(mosq, NULL, "device/orangepi5plus/uptime", 0);

        // 新增：泰山派3566订阅
        mosquitto_subscribe(mosq, NULL, "device/taishanpai/cpu", 0);
        mosquitto_subscribe(mosq, NULL, "device/taishanpai/mem", 0);
        mosquitto_subscribe(mosq, NULL, "device/taishanpai/temp", 0);
        mosquitto_subscribe(mosq, NULL, "device/taishanpai/net", 0);
        mosquitto_subscribe(mosq, NULL, "device/taishanpai/uptime", 0);

        // 新增：正点原子ATK3588订阅
        mosquitto_subscribe(mosq, NULL, "device/atk3588/cpu", 0);
        mosquitto_subscribe(mosq, NULL, "device/atk3588/mem", 0);
        mosquitto_subscribe(mosq, NULL, "device/atk3588/temp", 0);
        mosquitto_subscribe(mosq, NULL, "device/atk3588/net", 0);
        mosquitto_subscribe(mosq, NULL, "device/atk3588/uptime", 0);
    }
    else
    {
        mqtt_connected = 0;
        printf("MQTT: 连接失败 %s\n", mosquitto_strerror(rc));
    }
}

void mqtt_on_disconnect(struct mosquitto *mosq, void *userdata, int rc)
{
    mqtt_connected = 0;
    printf("MQTT: 断开连接 (code: %d)\n", rc);

    // ✅ 蜂鸣器滴滴两声提示（5秒内只响一次，避免频繁断开时一直响）
    static uint64_t last_beep_time = 0;
    uint64_t now = get_time_ms();

    if (now - last_beep_time > 5000)
    {                   // 5秒防抖动
        hal_beep(100);  // 第一声，100ms
        usleep(150000); // 间隔150ms
        hal_beep(100);  // 第二声，100ms
        last_beep_time = now;
    }
}
// == 简化：用ping测试网络 ==
static int is_network_ready(void)
{
    // 使用hostname获取IP，比getifaddrs简单可靠
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "hostname -I | grep -v '127.0.0.1' | grep -q '[0-9]'");

    int result = system(cmd);
    if (result == 0)
    {
        printf("Network: IP ready\n");
        return 1;
    }
    return 0;
}

// 获取毫秒时间戳
static uint64_t get_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

void mqtt_on_message(struct mosquitto *mosq, void *userdata, const struct mosquitto_message *msg)
{
    if (strcmp(msg->topic, "expansion/beep") == 0)
    {
        hal_beep(atoi((char *)msg->payload));
    }
    else if (strcmp(msg->topic, "expansion/led") == 0)
    {
        char *cmd = strdup((char *)msg->payload);
        char *led_id_str = strtok(cmd, ",");
        char *state_str = strtok(NULL, ",");

        if (led_id_str && state_str)
        {
            if (strcmp(led_id_str, "all") == 0)
            {
                if (atoi(state_str) == 0)
                    hal_led_all_off();
            }
            else
            {
                int led_id = atoi(led_id_str);
                if (led_id >= 0 && led_id < 3)
                    hal_led_set(led_id, atoi(state_str));
            }
        }
        free(cmd);
    }
    else if (strcmp(msg->topic, "expansion/led_rgb") == 0)
    {
        char *cmd = strdup((char *)msg->payload);
        int r = atoi(strtok(cmd, ","));
        int g = atoi(strtok(NULL, ","));
        int b = atoi(strtok(NULL, ","));
        hal_led_set(0, r);
        hal_led_set(1, g);
        hal_led_set(2, b);
        free(cmd);
    }
    else if (strncmp(msg->topic, "server/", 7) == 0)
    {
        dashboard_update_remote(msg->topic, (char *)msg->payload);
    }
    // 新增：处理香橙派和泰山派的数据（主题以device/开头）
    else if (strncmp(msg->topic, "device/", 7) == 0)
    {
        dashboard_update_remote(msg->topic, (char *)msg->payload);
    }
}

// 修改：初始化失败也不销毁，保留重试能力
int mqtt_client_init(const char *host, int port)
{
    if (mqtt_initialized && mosq)
        return 0;

    strncpy(broker_host, host, sizeof(broker_host) - 1);
    broker_port = port;

    mosquitto_lib_init();
    mosq = mosquitto_new(NULL, true, NULL);
    if (!mosq)
        return -1;

    mosquitto_username_pw_set(mosq, "admin", "19981009huan");
    mosquitto_message_callback_set(mosq, mqtt_on_message);
    mosquitto_connect_callback_set(mosq, mqtt_on_connect);
    mosquitto_disconnect_callback_set(mosq, mqtt_on_disconnect);

    printf("MQTT: 初始化完成，等待网络...\n");

    mqtt_initialized = 1;
    last_reconnect_attempt = get_time_ms();

    if (is_network_ready())
    {
        int ret = mosquitto_connect(mosq, broker_host, broker_port, 60);
        printf("MQTT: 初始连接 %s\n", ret == MOSQ_ERR_SUCCESS ? "成功" : "失败");
    }

    return 0;
}

void mqtt_client_cleanup(void)
{
    if (!mosq)
        return;

    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosq = NULL;
    mosquitto_lib_cleanup();

    mqtt_initialized = 0;
    mqtt_connected = 0;
}

int mqtt_publish(const char *topic, const char *payload)
{
    if (!mosq || !mqtt_connected)
    {
        return -1;
    }

    int ret = mosquitto_publish(mosq, NULL, topic, strlen(payload), payload, 0, false);
    if (ret != MOSQ_ERR_SUCCESS)
    {
        printf("MQTT: 发布失败 %s\n", mosquitto_strerror(ret));
        return -1;
    }
    return 0;
}

// == 核心：自动重连 ==
void mqtt_client_loop(int timeout_ms)
{
    if (!mosq)
        return;

    mosquitto_loop(mosq, timeout_ms, 1);

    if (!mqtt_connected)
    {
        uint64_t now = get_time_ms();
        uint64_t interval = now - last_reconnect_attempt;

        if (interval > RECONNECT_INTERVAL_MS)
        {
            if (is_network_ready())
            {
                printf("MQTT: 网络就绪，尝试连接 %s:%d\n", broker_host, broker_port);
                int ret = mosquitto_connect(mosq, broker_host, broker_port, 60);
                if (ret == MOSQ_ERR_SUCCESS)
                {
                    printf("MQTT: 连接成功\n");
                }
                else
                {
                    printf("MQTT: 连接失败 %s\n", mosquitto_strerror(ret));
                }
                last_reconnect_attempt = now;
            }
            else
            {
                printf("MQTT: 等待网络...\n");
                last_reconnect_attempt = now;
            }
        }
    }
}