#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <pthread.h>
#include <curl/curl.h>
#include "email_report.h"
#include "db_helper.h"

/* ==================== 全局配置（静态变量） ==================== */
static char g_recipient[128] = {0};
static int g_send_hour = 8;
static int g_send_minute = 0;
static time_t g_last_sent_date = 0;

/* ==================== 统计辅助函数 ==================== */
static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

static void calc_mean_std(const double *data, int n, double *mean, double *std)
{
    double sum = 0.0;
    for (int i = 0; i < n; i++)
        sum += data[i];
    *mean = sum / n;
    double var = 0.0;
    for (int i = 0; i < n; i++)
        var += (data[i] - *mean) * (data[i] - *mean);
    *std = sqrt(var / n);
}

static double calc_pearson(const double *x, const double *y, int n)
{
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0, sum_y2 = 0;
    for (int i = 0; i < n; i++)
    {
        sum_x += x[i];
        sum_y += y[i];
        sum_xy += x[i] * y[i];
        sum_x2 += x[i] * x[i];
        sum_y2 += y[i] * y[i];
    }
    double denom = (n * sum_x2 - sum_x * sum_x) * (n * sum_y2 - sum_y * sum_y);
    if (denom <= 0)
        return 0;
    return (n * sum_xy - sum_x * sum_y) / sqrt(denom);
}

static double calc_trend_per_day(const double *y, const double *x_days, int n)
{
    if (n < 2)
        return 0;
    double sum_x = 0, sum_y = 0, sum_xy = 0, sum_x2 = 0;
    for (int i = 0; i < n; i++)
    {
        sum_x += x_days[i];
        sum_y += y[i];
        sum_xy += x_days[i] * y[i];
        sum_x2 += x_days[i] * x_days[i];
    }
    double denominator = n * sum_x2 - sum_x * sum_x;
    if (fabs(denominator) < 1e-9)
        return 0;
    return (n * sum_xy - sum_x * sum_y) / denominator;
}

/* ==================== 分析函数 ==================== */
static void analyze_sensor_data(const double *temp, const double *humi, int n, report_analysis_t *res)
{
    if (n == 0)
        return;
    res->sensor_count = n;

    calc_mean_std(temp, n, &res->sensor_temp_mean, &res->sensor_temp_std);
    calc_mean_std(humi, n, &res->sensor_humi_mean, &res->sensor_humi_std);

    // 极值
    res->sensor_temp_min = res->sensor_temp_max = temp[0];
    res->sensor_humi_min = res->sensor_humi_max = humi[0];
    for (int i = 1; i < n; i++)
    {
        if (temp[i] < res->sensor_temp_min)
            res->sensor_temp_min = temp[i];
        if (temp[i] > res->sensor_temp_max)
            res->sensor_temp_max = temp[i];
        if (humi[i] < res->sensor_humi_min)
            res->sensor_humi_min = humi[i];
        if (humi[i] > res->sensor_humi_max)
            res->sensor_humi_max = humi[i];
    }

    res->sensor_pearson_corr = calc_pearson(temp, humi, n);

    // THI
    double thi_sum = 0;
    for (int i = 0; i < n; i++)
    {
        double thi = 0.8 * temp[i] + (0.01 * humi[i]) * (0.8 * temp[i] - 14.3) + 46.3;
        thi_sum += thi;
    }
    res->sensor_thi_mean = thi_sum / n;

    if (res->sensor_thi_mean < 55)
    {
        strcpy(res->comfort_grade, "Cold");
        strcpy(res->comfort_suggestion, "Warm");
    }
    else if (res->sensor_thi_mean < 60)
    {
        strcpy(res->comfort_grade, "Cool");
        strcpy(res->comfort_suggestion, "Add clothes");
    }
    else if (res->sensor_thi_mean < 65)
    {
        strcpy(res->comfort_grade, "Comfort");
        strcpy(res->comfort_suggestion, "Good");
    }
    else if (res->sensor_thi_mean < 70)
    {
        strcpy(res->comfort_grade, "Warm");
        strcpy(res->comfort_suggestion, "Ventilate");
    }
    else if (res->sensor_thi_mean < 75)
    {
        strcpy(res->comfort_grade, "Hot");
        strcpy(res->comfort_suggestion, "Fan/AC");
    }
    else
    {
        strcpy(res->comfort_grade, "Muggy");
        strcpy(res->comfort_suggestion, "Cool down");
    }

    // 3σ异常
    double temp_std3 = 3 * res->sensor_temp_std;
    double humi_std3 = 3 * res->sensor_humi_std;
    int ta = 0, ha = 0;
    for (int i = 0; i < n; i++)
    {
        if (fabs(temp[i] - res->sensor_temp_mean) > temp_std3)
            ta++;
        if (fabs(humi[i] - res->sensor_humi_mean) > humi_std3)
            ha++;
    }
    res->temp_anom_3sigma = ta;
    res->humi_anom_3sigma = ha;

    // 趋势（用索引作为天数）
    double *days = malloc(n * sizeof(double));
    for (int i = 0; i < n; i++)
        days[i] = i;
    res->temp_trend_per_day = calc_trend_per_day(temp, days, n);
    res->humi_trend_per_day = calc_trend_per_day(humi, days, n);
    free(days);
}

static void analyze_weather_data(const double *temp, const double *humi, const char **desc, int n, report_analysis_t *res)
{
    if (n == 0)
        return;
    res->weather_count = n;

    calc_mean_std(temp, n, &res->weather_temp_mean, &res->weather_temp_std);
    calc_mean_std(humi, n, &res->weather_humi_mean, &res->weather_humi_std);

    res->weather_temp_min = res->weather_temp_max = temp[0];
    res->weather_humi_min = res->weather_humi_max = humi[0];
    for (int i = 1; i < n; i++)
    {
        if (temp[i] < res->weather_temp_min)
            res->weather_temp_min = temp[i];
        if (temp[i] > res->weather_temp_max)
            res->weather_temp_max = temp[i];
        if (humi[i] < res->weather_humi_min)
            res->weather_humi_min = humi[i];
        if (humi[i] > res->weather_humi_max)
            res->weather_humi_max = humi[i];
    }

    // 统计最常见天气
    int max_count = 0;
    char most_common[30] = "unknown";
    for (int i = 0; i < n; i++)
    {
        int count = 1;
        for (int j = i + 1; j < n; j++)
        {
            if (desc[j] && desc[i] && strcmp(desc[i], desc[j]) == 0)
                count++;
        }
        if (count > max_count)
        {
            max_count = count;
            strncpy(most_common, desc[i] ? desc[i] : "unknown", sizeof(most_common) - 1);
        }
    }
    strncpy(res->most_common_weather, most_common, sizeof(res->most_common_weather) - 1);
}

static void compute_comparison(const double *sensor_temp, const double *sensor_humi, int sn,
                               const double *weather_temp, const double *weather_humi, int wn,
                               report_analysis_t *res)
{
    if (sn == 0 || wn == 0)
        return;

    double st_mean = 0, wht_mean = 0, sh_mean = 0, whh_mean = 0;
    for (int i = 0; i < sn; i++)
    {
        st_mean += sensor_temp[i];
        sh_mean += sensor_humi[i];
    }
    st_mean /= sn;
    sh_mean /= sn;
    for (int i = 0; i < wn; i++)
    {
        wht_mean += weather_temp[i];
        whh_mean += weather_humi[i];
    }
    wht_mean /= wn;
    whh_mean /= wn;

    res->temp_diff_mean = st_mean - wht_mean;
    res->humi_diff_mean = sh_mean - whh_mean;

    if (sn == wn && sn > 2)
    {
        res->temp_corr_with_weather = calc_pearson(sensor_temp, weather_temp, sn);
        res->humi_corr_with_weather = calc_pearson(sensor_humi, weather_humi, sn);
    }
    else
    {
        res->temp_corr_with_weather = 0;
        res->humi_corr_with_weather = 0;
    }
}

static void generate_report_text(const report_analysis_t *res, char *buffer, size_t buf_size)
{
    char temp[4096];
    int off = 0;
    
    // 标题
    off += snprintf(temp+off, sizeof(temp)-off,
                    "🌡️ 环境监测日报\n");
    off += snprintf(temp+off, sizeof(temp)-off,
                    "═══════════════════════════════════\n\n");
    
    // 日期
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char date_str[20];
    strftime(date_str, sizeof(date_str), "%Y年%m月%d日", &tm);
    off += snprintf(temp+off, sizeof(temp)-off, "📅 日期: %s\n\n", date_str);
    
    // ===== 室内传感器数据 =====
    off += snprintf(temp+off, sizeof(temp)-off, "🏠 室内传感器数据\n");
    off += snprintf(temp+off, sizeof(temp)-off, "────────────────────\n");
    off += snprintf(temp+off, sizeof(temp)-off, "📊 样本数: %d\n", res->sensor_count);
    
    // 温度（带图标）
    off += snprintf(temp+off, sizeof(temp)-off, "🌡️ 温度: 平均 %.1f℃, 标准差 %.2f, 范围 %.1f~%.1f℃\n",
                    res->sensor_temp_mean, res->sensor_temp_std,
                    res->sensor_temp_min, res->sensor_temp_max);
    
    // 湿度（带图标）
    off += snprintf(temp+off, sizeof(temp)-off, "💧 湿度: 平均 %.1f%%, 标准差 %.2f, 范围 %.0f~%.0f%%\n",
                    res->sensor_humi_mean, res->sensor_humi_std,
                    res->sensor_humi_min, res->sensor_humi_max);
    
    // 温湿度相关性
    off += snprintf(temp+off, sizeof(temp)-off, "🔗 温湿度相关性: %.3f\n", res->sensor_pearson_corr);
    
    // THI 舒适度
    off += snprintf(temp+off, sizeof(temp)-off, "😊 温湿指数(THI): %.1f", res->sensor_thi_mean);
    // 根据舒适度添加表情
    if (strcmp(res->comfort_grade, "Cold") == 0) {
        off += snprintf(temp+off, sizeof(temp)-off, " ❄️ 寒冷");
    } else if (strcmp(res->comfort_grade, "Cool") == 0) {
        off += snprintf(temp+off, sizeof(temp)-off, " 🧊 凉爽");
    } else if (strcmp(res->comfort_grade, "Comfort") == 0) {
        off += snprintf(temp+off, sizeof(temp)-off, " 😊 舒适");
    } else if (strcmp(res->comfort_grade, "Warm") == 0) {
        off += snprintf(temp+off, sizeof(temp)-off, " 🌤️ 温暖");
    } else if (strcmp(res->comfort_grade, "Hot") == 0) {
        off += snprintf(temp+off, sizeof(temp)-off, " 🔥 炎热");
    } else {
        off += snprintf(temp+off, sizeof(temp)-off, " 🥵 闷热");
    }
    off += snprintf(temp+off, sizeof(temp)-off, "\n");
    
    off += snprintf(temp+off, sizeof(temp)-off, "💡 建议: %s\n", res->comfort_suggestion);
    
    // 趋势
    off += snprintf(temp+off, sizeof(temp)-off, "📈 温度趋势: %+.2f ℃/天\n", res->temp_trend_per_day);
    off += snprintf(temp+off, sizeof(temp)-off, "📈 湿度趋势: %+.2f %% /天\n", res->humi_trend_per_day);
    
    // 异常
    if (res->temp_anom_3sigma > 0 || res->humi_anom_3sigma > 0) {
        off += snprintf(temp+off, sizeof(temp)-off, "⚠️ 异常(3σ): 温度 %d 个, 湿度 %d 个\n",
                        res->temp_anom_3sigma, res->humi_anom_3sigma);
    } else {
        off += snprintf(temp+off, sizeof(temp)-off, "✅ 无异常数据\n");
    }
    off += snprintf(temp+off, sizeof(temp)-off, "\n");
    
    // ===== 室外天气数据 =====
    off += snprintf(temp+off, sizeof(temp)-off, "☀️ 室外天气数据\n");
    off += snprintf(temp+off, sizeof(temp)-off, "────────────────────\n");
    off += snprintf(temp+off, sizeof(temp)-off, "📊 样本数: %d\n", res->weather_count);
    if (res->weather_count > 0) {
        // 天气图标
        const char *weather_icon = "🌤️";
        if (strstr(res->most_common_weather, "Clear")) weather_icon = "☀️";
        else if (strstr(res->most_common_weather, "Cloudy")) weather_icon = "☁️";
        else if (strstr(res->most_common_weather, "Rain")) weather_icon = "🌧️";
        else if (strstr(res->most_common_weather, "Snow")) weather_icon = "❄️";
        else if (strstr(res->most_common_weather, "Fog")) weather_icon = "🌫️";
        else if (strstr(res->most_common_weather, "Thunder")) weather_icon = "⛈️";
        
        off += snprintf(temp+off, sizeof(temp)-off, "%s 主要天气: %s\n", weather_icon, res->most_common_weather);
        off += snprintf(temp+off, sizeof(temp)-off, "🌡️ 温度: 平均 %.1f℃, 标准差 %.2f, 范围 %.1f~%.1f℃\n",
                        res->weather_temp_mean, res->weather_temp_std,
                        res->weather_temp_min, res->weather_temp_max);
        off += snprintf(temp+off, sizeof(temp)-off, "💧 湿度: 平均 %.1f%%, 标准差 %.2f, 范围 %.0f~%.0f%%\n",
                        res->weather_humi_mean, res->weather_humi_std,
                        res->weather_humi_min, res->weather_humi_max);
    } else {
        off += snprintf(temp+off, sizeof(temp)-off, "❌ 暂无天气数据\n");
    }
    off += snprintf(temp+off, sizeof(temp)-off, "\n");
    
    // ===== 室内外对比 =====
    off += snprintf(temp+off, sizeof(temp)-off, "🔄 室内外对比\n");
    off += snprintf(temp+off, sizeof(temp)-off, "────────────────────\n");
    if (res->sensor_count > 0 && res->weather_count > 0) {
        // 温度差
        if (res->temp_diff_mean > 2) {
            off += snprintf(temp+off, sizeof(temp)-off, "🌡️ 室内比室外高 %.1f℃ (室内更暖)\n", res->temp_diff_mean);
        } else if (res->temp_diff_mean < -2) {
            off += snprintf(temp+off, sizeof(temp)-off, "🌡️ 室内比室外低 %.1f℃ (室内更凉)\n", -res->temp_diff_mean);
        } else {
            off += snprintf(temp+off, sizeof(temp)-off, "🌡️ 室内外温度接近 (差 %.1f℃)\n", res->temp_diff_mean);
        }
        
        // 湿度差
        if (res->humi_diff_mean > 10) {
            off += snprintf(temp+off, sizeof(temp)-off, "💧 室内比室外湿 %.1f%%\n", res->humi_diff_mean);
        } else if (res->humi_diff_mean < -10) {
            off += snprintf(temp+off, sizeof(temp)-off, "💧 室内比室外干 %.1f%%\n", -res->humi_diff_mean);
        } else {
            off += snprintf(temp+off, sizeof(temp)-off, "💧 室内外湿度接近 (差 %.1f%%)\n", res->humi_diff_mean);
        }
        
        // 相关性
        if (res->sensor_count == res->weather_count && res->sensor_count > 2) {
            off += snprintf(temp+off, sizeof(temp)-off, "🔗 温度相关性: %.3f", res->temp_corr_with_weather);
            if (res->temp_corr_with_weather > 0.7) {
                off += snprintf(temp+off, sizeof(temp)-off, " (高度相关)\n");
            } else if (res->temp_corr_with_weather > 0.4) {
                off += snprintf(temp+off, sizeof(temp)-off, " (中等相关)\n");
            } else {
                off += snprintf(temp+off, sizeof(temp)-off, " (弱相关)\n");
            }
            off += snprintf(temp+off, sizeof(temp)-off, "🔗 湿度相关性: %.3f", res->humi_corr_with_weather);
            if (res->humi_corr_with_weather > 0.7) {
                off += snprintf(temp+off, sizeof(temp)-off, " (高度相关)\n");
            } else if (res->humi_corr_with_weather > 0.4) {
                off += snprintf(temp+off, sizeof(temp)-off, " (中等相关)\n");
            } else {
                off += snprintf(temp+off, sizeof(temp)-off, " (弱相关)\n");
            }
        }
    } else {
        off += snprintf(temp+off, sizeof(temp)-off, "❌ 数据不足，无法对比\n");
    }
    off += snprintf(temp+off, sizeof(temp)-off, "\n");
    
    // ===== 综合建议 =====
    off += snprintf(temp+off, sizeof(temp)-off, "💎 综合建议\n");
    off += snprintf(temp+off, sizeof(temp)-off, "────────────────────\n");
    if (res->sensor_count > 0 && res->weather_count > 0) {
        // 根据室内外对比生成建议
        if (res->temp_diff_mean > 3) {
            off += snprintf(temp+off, sizeof(temp)-off, "🪟 室内温度明显高于室外，建议开窗通风\n");
        } else if (res->temp_diff_mean < -3) {
            off += snprintf(temp+off, sizeof(temp)-off, "🔥 室内温度明显低于室外，建议适度采暖\n");
        }
        
        if (res->sensor_humi_mean > 70) {
            off += snprintf(temp+off, sizeof(temp)-off, "💨 室内湿度过高，建议使用除湿机或开窗\n");
        } else if (res->sensor_humi_mean < 30) {
            off += snprintf(temp+off, sizeof(temp)-off, "💦 室内湿度过低，建议使用加湿器\n");
        }
        
        // 根据舒适度建议
        if (strcmp(res->comfort_grade, "Hot") == 0 || strcmp(res->comfort_grade, "Muggy") == 0) {
            off += snprintf(temp+off, sizeof(temp)-off, "❄️ 建议开启空调或风扇降温\n");
        } else if (strcmp(res->comfort_grade, "Cold") == 0) {
            off += snprintf(temp+off, sizeof(temp)-off, "🧣 建议增加衣物或开启取暖设备\n");
        }
    } else {
        off += snprintf(temp+off, sizeof(temp)-off, "📊 数据积累中，请持续监测\n");
    }
    
    // 结尾
    off += snprintf(temp+off, sizeof(temp)-off, "\n────────────────────\n");
    off += snprintf(temp+off, sizeof(temp)-off, "📱 系统将持续监测，祝您生活愉快！\n");
    
    strncpy(buffer, temp, buf_size-1);
    buffer[buf_size-1] = '\0';
}

/* ==================== 邮件发送（使用 libcurl） ==================== */

// 用于读取邮件内容的回调函数
struct email_payload
{
    char *data;
    size_t offset;
};

static size_t email_read_callback(char *buffer, size_t size, size_t nitems, void *userp)
{
    struct email_payload *payload = (struct email_payload *)userp;
    size_t buffer_size = size * nitems;
    size_t remaining = strlen(payload->data) - payload->offset;

    if (remaining == 0)
        return 0;

    size_t to_copy = remaining < buffer_size ? remaining : buffer_size;
    memcpy(buffer, payload->data + payload->offset, to_copy);
    payload->offset += to_copy;

    return to_copy;
}

static int send_email(const char *subject, const char *body)
{
    if (strlen(g_recipient) == 0) {
        fprintf(stderr, "[EMAIL] Recipient not set\n");
        return -1;
    }

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "echo '%s' | python3 /home/cat/expansion/scripts/send_email.py '%s' '%s'",
             body, g_recipient, subject);

    int ret = system(cmd);
    if (ret != 0) {
        fprintf(stderr, "[EMAIL] Python send failed, ret=%d\n", ret);
        return -1;
    }

    printf("[EMAIL] Report sent successfully to %s\n", g_recipient);
    return 0;
}
/* ==================== 获取某天的起止时间戳 ==================== */
static void get_day_range(time_t *start, time_t *end, time_t day_ts)
{
    struct tm tm;
    localtime_r(&day_ts, &tm);
    tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    *start = mktime(&tm);
    *end = *start + 86400 - 1;
}

/* ==================== 公共接口 ==================== */
void email_report_init(const char *recipient, int hour, int minute)
{
    if (recipient)
        strncpy(g_recipient, recipient, sizeof(g_recipient) - 1);
    g_send_hour = hour;
    g_send_minute = minute;
    g_last_sent_date = 0;
    curl_global_init(CURL_GLOBAL_ALL);
    printf("[EMAIL] Initialized, will send to %s at %02d:%02d\n", g_recipient, hour, minute);
}

void email_send_daily_report(void)
{
    if (strlen(g_recipient) == 0)
    {
        fprintf(stderr, "[EMAIL] Recipient not set\n");
        return;
    }

    // 分析昨天的数据
    time_t now = time(NULL);
    time_t yesterday = now - 86400;
    time_t start_ts, end_ts;
    get_day_range(&start_ts, &end_ts, yesterday);

    // 查询传感器数据
    double *sensor_temp, *sensor_humi;
    int sensor_count;
    if (db_get_sensor_data_range(start_ts, end_ts, &sensor_temp, &sensor_humi, &sensor_count) != 0)
    {
        fprintf(stderr, "[EMAIL] Failed to get sensor data\n");
        return;
    }

    // 查询天气数据
    double *weather_temp, *weather_humi;
    char **weather_desc;
    int weather_count;
    if (db_get_weather_data_range(start_ts, end_ts, &weather_temp, &weather_humi, &weather_desc, &weather_count) != 0)
    {
        fprintf(stderr, "[EMAIL] Failed to get weather data\n");
        weather_count = 0; // 继续
    }

    report_analysis_t res = {0};
    if (sensor_count > 0)
    {
        analyze_sensor_data(sensor_temp, sensor_humi, sensor_count, &res);
        res.sensor_count = sensor_count;
    }
    if (weather_count > 0)
    {
        analyze_weather_data(weather_temp, weather_humi, (const char **)weather_desc, weather_count, &res);
        res.weather_count = weather_count;
    }
    if (sensor_count > 0 && weather_count > 0)
    {
        compute_comparison(sensor_temp, sensor_humi, sensor_count,
                           weather_temp, weather_humi, weather_count, &res);
    }

    // 释放内存
    free(sensor_temp);
    free(sensor_humi);
    free(weather_temp);
    free(weather_humi);
    for (int i = 0; i < weather_count; i++)
        free(weather_desc[i]);
    free(weather_desc);

    char report_body[4096];
    generate_report_text(&res, report_body, sizeof(report_body));

    char subject[128];
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(subject, sizeof(subject), "📊 环境监测日报 %Y年%m月%d日", &tm);
    send_email(subject, report_body);
}

void *email_report_thread(void *arg)
{
    (void)arg;
    printf("[EMAIL] Thread started, checking every 30 seconds\n");
    while (1)
    {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);

        if (tm.tm_hour == g_send_hour && tm.tm_min == g_send_minute)
        {
            time_t today_zero = now - tm.tm_sec - tm.tm_min * 60 - tm.tm_hour * 3600;
            if (g_last_sent_date != today_zero)
            {
                printf("[EMAIL] Sending daily report...\n");
                email_send_daily_report();
                g_last_sent_date = today_zero;
            }
        }
        sleep(30);
    }
    return NULL;
}
/* ==================== 快速测试函数（直接调用） ==================== */
void email_test_send(void)
{
    const char *to = "2656086785@qq.com";
    const char *subject = "Test Email from System";
    const char *body = "Hello!\n\nThis is a test email.\n\nIf you received this, the SMTP is working correctly.\n\nTime: " __DATE__ " " __TIME__;

    printf("[TEST] Sending test email to %s...\n", to);

    // 临时设置收件人
    strncpy(g_recipient, to, sizeof(g_recipient) - 1);

    int ret = send_email(subject, body);
    if (ret == 0)
    {
        printf("[TEST] ✅ Email sent! Check your inbox.\n");
    }
    else
    {
        printf("[TEST] ❌ Send failed.\n");
    }
}