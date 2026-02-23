/*
 * threads.c - 线程调度中心
 * 职责：创建线程，调用monitor接口，发布MQTT，天气异步请求
 * 优化点：合并重复的MQTT发布线程，减少资源开销，增加优雅退出机制
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <stdarg.h>  // 新增：必须添加，用于va_start/va_end
#include "threads.h"
#include "mqtt_client.h"
#include "hal_gpio.h"
#include "hal_system.h"
#include "system_monitor.h"
#include "hal_dht11.h"
#include "network_monitor.h"
#include "menu.h"
#include "hal_oled.h"

/* ==================== 全局配置 & 常量定义 ==================== */
// 天气API配置
#define DEFAULT_LATITUDE  "34.62"
#define DEFAULT_LONGITUDE "112.45"
#define WEATHER_API_URL   "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s&current=temperature_2m,relative_humidity_2m,weather_code&timezone=Asia/Shanghai"
#define LOCATION_API_URL  "http://ip-api.com/json/?fields=lat,lon"
#define HTTP_RECV_BUF_SIZE  8192
#define WEATHER_ICON_SIZE 28

// 线程运行控制（全局标志位，支持优雅退出）
static int g_threads_running = 0;
// 发布频率（毫秒），统一控制所有MQTT发布间隔
#define PUBLISH_INTERVAL_MS 1000

// 天气图标声明
extern const unsigned char icon_weather_clear[];
extern const unsigned char icon_weather_cloudy[];
extern const unsigned char icon_weather_rain[];
extern const unsigned char icon_weather_snow[];
extern const unsigned char icon_weather_fog[];
extern const unsigned char icon_weather_thunderstorm[];
extern const unsigned char icon_weather_unknown[];

/* ==================== 天气相关全局变量（互斥锁保护） ==================== */
static pthread_mutex_t weather_mutex = PTHREAD_MUTEX_INITIALIZER;
static char weather_latitude[16] = {0};
static char weather_longitude[16] = {0};
static int weather_need_refresh = 0;
static char weather_address[64] = {0};

// 天气数据结构体
typedef struct {
    char weather_text[20];
    float temp;
    float humi;
    char update_time[20];
    int weather_code;
    int is_valid;
} network_weather_t;
network_weather_t net_weather = {0}; // 供UI访问

/* ==================== 工具函数封装 ==================== */
// 通用MQTT发布函数（封装重复逻辑）
static void mqtt_safe_publish(const char *topic, const char *fmt, ...)
{
    if (!topic || !fmt) return;
    
    char msg[128] = {0};
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg)-1, fmt, args);
    va_end(args);
    
    mqtt_publish(topic, msg);
}

// 检测网络是否就绪
static int is_network_ready(void)
{
    network_state_t state;
    network_monitor_get_state(&state);
    return (state.wifi_connected && state.wifi_internet);
}

// 等待网络就绪（最多30秒，超时用默认地址）
static void wait_network_ready(void)
{
    int wait_sec = 0;
    printf("[WEATHER] Waiting for network ready...\n");
    
    while (!is_network_ready() && g_threads_running)
    {
        wait_sec += 2;
        if (wait_sec > 30)
        {
            printf("[WEATHER] Network timeout after 30s, use default location\n");
            pthread_mutex_lock(&weather_mutex);
            strncpy(weather_latitude, DEFAULT_LATITUDE, sizeof(weather_latitude)-1);
            strncpy(weather_longitude, DEFAULT_LONGITUDE, sizeof(weather_longitude)-1);
            strncpy(weather_address, "Luoyang, Henan", sizeof(weather_address)-1);
            pthread_mutex_unlock(&weather_mutex);
            return;
        }
        sleep(2);
    }
    printf("[WEATHER] Network is ready!\n");
}

/* ==================== HTTP回调 & 天气解析工具 ==================== */
static size_t http_write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t real_size = size * nmemb;
    char *recv_buf = (char *)userp;
    size_t buf_used = strlen(recv_buf);
    
    if (buf_used + real_size >= HTTP_RECV_BUF_SIZE - 1) {
        real_size = HTTP_RECV_BUF_SIZE - 1 - buf_used;
    }
    memcpy(recv_buf + buf_used, contents, real_size);
    recv_buf[buf_used + real_size] = '\0';
    return real_size;
}

static const char* weather_code_to_text(int code)
{
    if (code == 0) return "Clear";
    if (code == 1 || code == 2 || code == 3) return "Cloudy";
    if (code >= 45 && code <= 48) return "Foggy";
    if (code >= 51 && code <= 67) return "Rain";
    if (code >= 71 && code <= 86) return "Snow";
    if (code >= 95) return "Thunderstorm";
    return "Unknown";
}

/* ==================== 天气异步请求逻辑（保持原有逻辑） ==================== */
void trigger_weather_refresh(void)
{
    weather_need_refresh = 1;
    printf("[WEATHER] Trigger async refresh\n");
}

static void auto_get_location_async(void)
{
    if (!g_threads_running) return;
    
    int retry = 3;
    char lat[16] = {0};
    char lon[16] = {0};
    char addr[64] = {0};

    // 默认值
    strncpy(lat, DEFAULT_LATITUDE, sizeof(lat)-1);
    strncpy(lon, DEFAULT_LONGITUDE, sizeof(lon)-1);
    strncpy(addr, "Luoyang, Henan", sizeof(addr)-1);

    while (retry-- && g_threads_running)
    {
        CURL *curl = curl_easy_init();
        char *http_buf = calloc(1, HTTP_RECV_BUF_SIZE);
        if (!curl || !http_buf) {
            printf("[LOCATION] Retry %d: init failed\n", retry);
            curl_easy_cleanup(curl);
            free(http_buf);
            sleep(1);
            continue;
        }

        // 配置CURL（请求城市+省份）
        curl_easy_setopt(curl, CURLOPT_URL, "http://ip-api.com/json/?fields=lat,lon,city,regionName");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, http_buf);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            cJSON *json_root = cJSON_Parse(http_buf);
            if (json_root) {
                cJSON *lat_item = cJSON_GetObjectItem(json_root, "lat");
                cJSON *lon_item = cJSON_GetObjectItem(json_root, "lon");
                cJSON *city_item = cJSON_GetObjectItem(json_root, "city");
                cJSON *region_item = cJSON_GetObjectItem(json_root, "regionName");

                // 解析经纬度
                if (cJSON_IsNumber(lat_item) && cJSON_IsNumber(lon_item)) {
                    snprintf(lat, sizeof(lat), "%.2f", lat_item->valuedouble);
                    snprintf(lon, sizeof(lon), "%.2f", lon_item->valuedouble);
                }

                // 解析地址
                if (cJSON_IsString(city_item) && cJSON_IsString(region_item)) {
                    snprintf(addr, sizeof(addr), "%s, %s", city_item->valuestring, region_item->valuestring);
                } else if (cJSON_IsString(city_item)) {
                    strncpy(addr, city_item->valuestring, sizeof(addr)-1);
                } else if (cJSON_IsString(region_item)) {
                    strncpy(addr, region_item->valuestring, sizeof(addr)-1);
                }

                cJSON_Delete(json_root);
                curl_easy_cleanup(curl);
                free(http_buf);
                break;
            }
        }

        curl_easy_cleanup(curl);
        free(http_buf);
        printf("[LOCATION] Retry %d failed, res=%d\n", retry, res);
        sleep(1);
    }

    // 更新全局变量
    pthread_mutex_lock(&weather_mutex);
    strncpy(weather_latitude, lat, sizeof(weather_latitude)-1);
    strncpy(weather_longitude, lon, sizeof(weather_longitude)-1);
    strncpy(weather_address, addr, sizeof(weather_address)-1);
    pthread_mutex_unlock(&weather_mutex);

    printf("[LOCATION] Async done: lat=%s, lon=%s, addr=%s\n", weather_latitude, weather_longitude, weather_address);
}

void get_weather_address(char *addr, int buf_size)
{
    if (!addr || buf_size <= 0) return;
    pthread_mutex_lock(&weather_mutex);
    strncpy(addr, weather_address, buf_size-1);
    pthread_mutex_unlock(&weather_mutex);
}

static void get_weather_async(void)
{
    if (!g_threads_running || strlen(weather_latitude) == 0 || strlen(weather_longitude) == 0) {
        printf("[WEATHER] No location or thread stopped, skip\n");
        return;
    }

    int retry = 3;
    network_weather_t temp_weather = {0};

    while (retry-- && g_threads_running)
    {
        char api_url[512] = {0};
        pthread_mutex_lock(&weather_mutex);
        snprintf(api_url, sizeof(api_url), WEATHER_API_URL, weather_latitude, weather_longitude);
        pthread_mutex_unlock(&weather_mutex);

        CURL *curl = curl_easy_init();
        char *http_buf = calloc(1, HTTP_RECV_BUF_SIZE);
        if (!curl || !http_buf) {
            printf("[WEATHER] Retry %d: init failed\n", retry);
            curl_easy_cleanup(curl);
            free(http_buf);
            sleep(1);
            continue;
        }

        // 配置CURL
        curl_easy_setopt(curl, CURLOPT_URL, api_url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, http_buf);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code == 200) {
                cJSON *json_root = cJSON_Parse(http_buf);
                if (json_root) {
                    cJSON *current_item = cJSON_GetObjectItem(json_root, "current");
                    if (cJSON_IsObject(current_item)) {
                        cJSON *temp_item = cJSON_GetObjectItem(current_item, "temperature_2m");
                        cJSON *humi_item = cJSON_GetObjectItem(current_item, "relative_humidity_2m");
                        cJSON *code_item = cJSON_GetObjectItem(current_item, "weather_code");
                        cJSON *time_item = cJSON_GetObjectItem(current_item, "time");

                        if (temp_item) temp_weather.temp = temp_item->valuedouble;
                        if (humi_item) temp_weather.humi = humi_item->valuedouble;
                        if (code_item) {
                            temp_weather.weather_code = (int)code_item->valuedouble;
                            strncpy(temp_weather.weather_text, weather_code_to_text(temp_weather.weather_code), sizeof(temp_weather.weather_text)-1);
                        }
                        if (time_item) {
                            strncpy(temp_weather.update_time, time_item->valuestring + 5, 11);
                            temp_weather.update_time[2] = '/';
                            temp_weather.update_time[5] = ' ';
                        }
                        temp_weather.is_valid = 1;
                    }
                    cJSON_Delete(json_root);
                }
            }
        }

        curl_easy_cleanup(curl);
        free(http_buf);
        
        if (temp_weather.is_valid) break; // 成功获取则退出重试
        printf("[WEATHER] Retry %d failed, res=%d\n", retry, res);
        sleep(1);
    }

    // 更新全局天气数据
    pthread_mutex_lock(&weather_mutex);
    memcpy(&net_weather, &temp_weather, sizeof(network_weather_t));
    pthread_mutex_unlock(&weather_mutex);

    printf("[WEATHER] Async done: %.1fC, %.1f%%, valid=%d\n", net_weather.temp, net_weather.humi, net_weather.is_valid);
}

/* ==================== 合并后的核心线程实现 ==================== */
// MQTT客户端线程（保留，负责MQTT核心循环）
void *mqtt_thread(void *arg)
{
    (void)arg;
    printf("[MQTT] Thread started\n");
    
    while (g_threads_running)
    {
        mqtt_client_loop(10);
        usleep(10000); // 降低循环频率，减少CPU占用
    }
    
    printf("[MQTT] Thread stopped\n");
    return NULL;
}

// 统一数据发布线程（合并sensor/system/network发布逻辑）
void *data_publish_thread(void *arg)
{
    (void)arg;
    printf("[DATA-PUB] Thread started (merged sensor/system/network)\n");
    
    system_monitor_start(); // 初始化系统监控
    
    while (g_threads_running)
    {
        // 1. 发布传感器数据（DHT11）
        float temp, hum;
        if (hal_dht11_read(&temp, &hum) == 0)
        {
            mqtt_safe_publish("sensor/dht11", "Temp:%.1f,Humi:%.1f", temp, hum);
        }

        // 2. 发布系统监控数据
        system_state_t sys_state;
        system_monitor_get_state(&sys_state);
        mqtt_safe_publish("system/cpu", "CPU:%d%%", sys_state.cpu_total_usage);
        mqtt_safe_publish("system/mem", "MEM:%d%%", sys_state.memory.used_percent);
        mqtt_safe_publish("system/temp", "TEMP:%dC", sys_state.gpu.temp_c);

        // 3. 发布网络监控数据
        network_state_t net_state;
        network_monitor_get_state(&net_state);
        if (net_state.wifi_connected)
        {
            mqtt_safe_publish("network/wifi", "SSID:%s,IP:%s,SIG:%ddBm,INET:%d",
                             net_state.wifi_ssid, net_state.wifi_ip, net_state.wifi_signal, net_state.wifi_internet);
        }
        mqtt_safe_publish("network/lan", "LAN:%d", net_state.lan_device_count);

        // 统一休眠（替代多个线程的独立sleep）
        usleep(PUBLISH_INTERVAL_MS * 1000);
    }
    
    printf("[DATA-PUB] Thread stopped\n");
    return NULL;
}

// 按键处理线程（保留，独立高频检测）
void *button_thread(void *arg)
{
    (void)arg;
    printf("[BUTTON] Thread started\n");
    hal_gpio_init();

    while (g_threads_running)
    {
        int btn = hal_button_read();
        if (btn >= 0)
        {
            hal_led_set(0, 1);
            event_t ev;
            uint32_t dur = hal_button_wait_release(btn);

            switch (btn)
            {
            case 0: ev = EV_UP; break;
            case 1: ev = (dur > 200) ? EV_BACK : EV_ENTER; break;
            case 2: ev = EV_DOWN; break;
            default: continue;
            }

            menu_handle_event(ev);
            hal_led_set(0, 0);
        }
        usleep(50000); // 10ms检测一次，保证按键响应速度
    }
    
    printf("[BUTTON] Thread stopped\n");
    return NULL;
}

// 天气工作线程（保留，优化退出逻辑）
void *weather_worker_thread(void *arg)
{
    (void)arg;
    printf("[WEATHER] Thread started\n");

    // 初始化CURL
    curl_global_init(CURL_GLOBAL_ALL);

    // 等待网络就绪
    wait_network_ready();

    // 首次获取定位+天气
    if (g_threads_running) {
        auto_get_location_async();
        get_weather_async();
    }

    // 主循环
    while (g_threads_running)
    {
        if (weather_need_refresh)
        {
            if (is_network_ready()) {
                auto_get_location_async();
                get_weather_async();
            } else {
                printf("[WEATHER] Refresh skipped: network not ready\n");
                pthread_mutex_lock(&weather_mutex);
                net_weather.is_valid = 0;
                pthread_mutex_unlock(&weather_mutex);
            }
            weather_need_refresh = 0;
        }
        usleep(100000); // 100ms检测一次刷新请求
    }

    // 资源清理
    curl_global_cleanup();
    pthread_mutex_destroy(&weather_mutex);
    printf("[WEATHER] Thread stopped\n");
    return NULL;
}

/* ==================== 线程管理（优化版） ==================== */
static void create_thread(pthread_t *t, void *(*f)(void *), const char *name)
{
    if (!g_threads_running) return;
    
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

// 初始化所有线程
void threads_init(void)
{
    static int initialized = 0;
    if (initialized) return;

    printf("[THREAD] Initializing all threads...\n");
    g_threads_running = 1;
    network_monitor_init();

    // 创建优化后的线程（从6个减少到4个）
    pthread_t mqtt_t, publish_t, btn_t, weather_t;
    create_thread(&mqtt_t, mqtt_thread, "MQTT");
    create_thread(&publish_t, data_publish_thread, "DATA-PUBLISH"); // 合并后的发布线程
    create_thread(&btn_t, button_thread, "BUTTON");
    create_thread(&weather_t, weather_worker_thread, "WEATHER");

    initialized = 1;
    printf("[THREAD] All threads initialized (optimized: 4 threads total)\n");
}

// 优雅停止所有线程（新增接口）
void threads_stop(void)
{
    printf("[THREAD] Stopping all threads...\n");
    g_threads_running = 0;
    sleep(1); // 等待线程完成收尾工作
    printf("[THREAD] All threads stopped\n");
}

/* ==================== 对外接口（保持原有兼容） ==================== */
void get_weather_data(network_weather_t *out_weather)
{
    if (!out_weather) return;
    pthread_mutex_lock(&weather_mutex);
    memcpy(out_weather, &net_weather, sizeof(network_weather_t));
    pthread_mutex_unlock(&weather_mutex);
}

void get_weather_location(char *lat, char *lon, int buf_size)
{
    if (!lat || !lon) return;
    pthread_mutex_lock(&weather_mutex);
    strncpy(lat, weather_latitude, buf_size-1);
    strncpy(lon, weather_longitude, buf_size-1);
    pthread_mutex_unlock(&weather_mutex);
}