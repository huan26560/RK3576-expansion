/*
 * threads.c - 线程调度中心
 * 职责：创建线程，调用monitor接口，发布MQTT，天气异步请求
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
#include "threads.h"
#include "mqtt_client.h"
#include "hal_gpio.h"
#include "hal_system.h"
#include "system_monitor.h"
#include "hal_dht11.h"
#include "network_monitor.h"
#include "menu.h"
#include "hal_oled.h" // 天气UI相关

/* ==================== 全局配置 & 天气相关定义 ==================== */
// 天气API配置
#define DEFAULT_LATITUDE  "34.62"
#define DEFAULT_LONGITUDE "112.45"
#define WEATHER_API_URL   "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s&current=temperature_2m,relative_humidity_2m,weather_code&timezone=Asia/Shanghai"
#define LOCATION_API_URL  "http://ip-api.com/json/?fields=lat,lon"
#define HTTP_RECV_BUF_SIZE  8192
#define WEATHER_ICON_SIZE 28

// 天气图标声明（和你的page_env.c保持一致）
extern const unsigned char icon_weather_clear[];
extern const unsigned char icon_weather_cloudy[];
extern const unsigned char icon_weather_rain[];
extern const unsigned char icon_weather_snow[];
extern const unsigned char icon_weather_fog[];
extern const unsigned char icon_weather_thunderstorm[];
extern const unsigned char icon_weather_unknown[];

// 天气线程全局变量（互斥锁保护）
static pthread_mutex_t weather_mutex = PTHREAD_MUTEX_INITIALIZER;
static char weather_latitude[16] = {0};
static char weather_longitude[16] = {0};
static int weather_thread_running = 0;
static int weather_need_refresh = 0;
static char weather_address[64] = {0}; // 新增：拼音地址（最大64字节）
// 天气数据结构体（供page_env.c访问）
typedef struct {
    char weather_text[20];
    float temp;
    float humi;
    char update_time[20];
    int weather_code;
    int is_valid;
} network_weather_t;
network_weather_t net_weather = {0}; // 全局暴露，供UI访问

/* ==================== 工具函数（天气相关） ==================== */
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

// 对外声明：触发天气刷新（供page_env.c调用）
void trigger_weather_refresh(void)
{
    weather_need_refresh = 1;
    printf("[WEATHER] Trigger async refresh\n");
}

/* ==================== 天气异步请求函数 ==================== */
static void auto_get_location_async(void)
{
    CURL *curl = NULL;
    CURLcode res;
    char *http_buf = NULL;
    cJSON *json_root = NULL;
    char lat[16] = {0};
    char lon[16] = {0};
    char addr[64] = {0}; // 临时地址变量

    // 默认先用洛阳经纬度+地址
    strncpy(lat, DEFAULT_LATITUDE, sizeof(lat)-1);
    strncpy(lon, DEFAULT_LONGITUDE, sizeof(lon)-1);
    strncpy(addr, "Luoyang, Henan", sizeof(addr)-1); // 默认地址

    // 分配缓冲区
    http_buf = (char *)calloc(1, HTTP_RECV_BUF_SIZE);
    if (http_buf == NULL) goto exit;

    // 初始化CURL
    curl = curl_easy_init();
    if (curl == NULL) goto exit;

    // 配置CURL（请求字段增加 city,regionName）
    // 注意：修改 LOCATION_API_URL 为 "http://ip-api.com/json/?fields=lat,lon,city,regionName"
    curl_easy_setopt(curl, CURLOPT_URL, "http://ip-api.com/json/?fields=lat,lon,city,regionName");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, http_buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 2);

    // 发送请求
    res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        json_root = cJSON_Parse(http_buf);
        if (json_root) {
            cJSON *lat_item = cJSON_GetObjectItem(json_root, "lat");
            cJSON *lon_item = cJSON_GetObjectItem(json_root, "lon");
            cJSON *city_item = cJSON_GetObjectItem(json_root, "city");       // 新增：城市
            cJSON *region_item = cJSON_GetObjectItem(json_root, "regionName");// 新增：省份

            // 解析经纬度
            if (cJSON_IsNumber(lat_item) && cJSON_IsNumber(lon_item)) {
                snprintf(lat, sizeof(lat), "%.2f", lat_item->valuedouble);
                snprintf(lon, sizeof(lon), "%.2f", lon_item->valuedouble);
            }

            // 新增：解析拼音地址（格式："City, Region"）
            if (cJSON_IsString(city_item) && cJSON_IsString(region_item)) {
                snprintf(addr, sizeof(addr), "%s, %s", city_item->valuestring, region_item->valuestring);
            } else if (cJSON_IsString(city_item)) {
                strncpy(addr, city_item->valuestring, sizeof(addr)-1);
            } else if (cJSON_IsString(region_item)) {
                strncpy(addr, region_item->valuestring, sizeof(addr)-1);
            }
        }
    }

exit:
    // 加锁更新全局变量
    pthread_mutex_lock(&weather_mutex);
    strncpy(weather_latitude, lat, sizeof(weather_latitude)-1);
    strncpy(weather_longitude, lon, sizeof(weather_longitude)-1);
    strncpy(weather_address, addr, sizeof(weather_address)-1); // 新增：更新地址
    pthread_mutex_unlock(&weather_mutex);

    // 资源清理
    if (json_root) cJSON_Delete(json_root);
    if (curl) curl_easy_cleanup(curl);
    if (http_buf) free(http_buf);
    printf("[LOCATION] Async done: lat=%s, lon=%s, addr=%s\n", weather_latitude, weather_longitude, weather_address);
}
void get_weather_address(char *addr, int buf_size)
{
    if (addr == NULL || buf_size <= 0) return;
    pthread_mutex_lock(&weather_mutex);
    strncpy(addr, weather_address, buf_size-1);
    pthread_mutex_unlock(&weather_mutex);
}
static void get_weather_async(void)
{
    if (strlen(weather_latitude) == 0 || strlen(weather_longitude) == 0) {
        printf("[WEATHER] No location, skip\n");
        return;
    }

    CURL *curl = NULL;
    CURLcode res;
    char *http_buf = NULL;
    cJSON *json_root = NULL;
    char api_url[512] = {0};
    network_weather_t temp_weather = {0};

    // 构建API URL（加锁读取经纬度）
    pthread_mutex_lock(&weather_mutex);
    snprintf(api_url, sizeof(api_url), WEATHER_API_URL, weather_latitude, weather_longitude);
    pthread_mutex_unlock(&weather_mutex);

    // 分配缓冲区
    http_buf = (char *)calloc(1, HTTP_RECV_BUF_SIZE);
    if (http_buf == NULL) goto exit;

    // 初始化CURL
    curl = curl_easy_init();
    if (curl == NULL) goto exit;

    // 配置CURL
    curl_easy_setopt(curl, CURLOPT_URL, api_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, http_buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    // 发送请求
    res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code == 200) {
            json_root = cJSON_Parse(http_buf);
            if (json_root) {
                cJSON *current_item = cJSON_GetObjectItem(json_root, "current");
                if (cJSON_IsObject(current_item)) {
                    // 解析天气数据
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
            }
        }
    }


exit:
    // 加锁更新全局天气数据
    pthread_mutex_lock(&weather_mutex);
    memcpy(&net_weather, &temp_weather, sizeof(network_weather_t));
    pthread_mutex_unlock(&weather_mutex);

    // 资源清理
    if (json_root) cJSON_Delete(json_root);
    if (curl) curl_easy_cleanup(curl);
    if (http_buf) free(http_buf);
    printf("[WEATHER] Async done: %.1fC, %.1f%%\n", net_weather.temp, net_weather.humi);
}

/* ==================== 原有线程实现 ==================== */
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
        snprintf(msg, sizeof(msg), "CPU:%d%%", state.cpu_total_usage);
        mqtt_publish("system/cpu", msg);

        snprintf(msg, sizeof(msg), "MEM:%d%%", state.memory.used_percent);
        mqtt_publish("system/mem", msg);

        snprintf(msg, sizeof(msg), "TEMP:%dC", state.gpu.temp_c);
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
    return NULL;
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

/* ==================== 天气线程实现 ==================== */
void *weather_worker_thread(void *arg)
{
    (void)arg;
    weather_thread_running = 1;
    printf("[WEATHER] Thread started\n");

    // 初始化CURL
    curl_global_init(CURL_GLOBAL_ALL);

    // 首次启动：获取定位+天气
    auto_get_location_async();
    get_weather_async();

    // 线程主循环（低CPU占用）
    while (weather_thread_running)
    {
        if (weather_need_refresh)
        {
            auto_get_location_async();
            get_weather_async();
            weather_need_refresh = 0;
        }
        usleep(100000); // 休眠100ms
    }

    // 资源清理
    curl_global_cleanup();
    pthread_mutex_destroy(&weather_mutex);
    printf("[WEATHER] Thread stopped\n");
    pthread_exit(NULL);
    return NULL;
}

/* ==================== 线程管理（统一初始化） ==================== */
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

    // 创建所有线程（包含新增的天气线程）
    pthread_t mqtt_t, sensor_t, sys_t, net_t, btn_t, weather_t;
    create_thread(&mqtt_t, mqtt_thread, "MQTT");
    create_thread(&sensor_t, sensor_thread, "SENSOR");
    create_thread(&sys_t, system_publish_thread, "SYSTEM");
    create_thread(&net_t, network_publish_thread, "NETWORK");
    create_thread(&btn_t, button_thread, "BUTTON");
    create_thread(&weather_t, weather_worker_thread, "WEATHER"); // 新增天气线程

    initialized = 1;
    printf("[THREAD] All threads initialized\n");
}

// 对外声明：获取天气数据（供page_env.c调用，加锁保证线程安全）
void get_weather_data(network_weather_t *out_weather)
{
    if (out_weather == NULL) return;
    pthread_mutex_lock(&weather_mutex);
    memcpy(out_weather, &net_weather, sizeof(network_weather_t));
    pthread_mutex_unlock(&weather_mutex);
}

// 对外声明：获取经纬度（供page_env.c调用）
void get_weather_location(char *lat, char *lon, int buf_size)
{
    if (lat == NULL || lon == NULL) return;
    pthread_mutex_lock(&weather_mutex);
    strncpy(lat, weather_latitude, buf_size-1);
    strncpy(lon, weather_longitude, buf_size-1);
    pthread_mutex_unlock(&weather_mutex);
}