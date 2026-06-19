#ifndef EMAIL_REPORT_H
#define EMAIL_REPORT_H

#include <time.h>

typedef struct {
    // 传感器统计
    int sensor_count;
    double sensor_temp_mean, sensor_temp_std, sensor_temp_min, sensor_temp_max;
    double sensor_humi_mean, sensor_humi_std, sensor_humi_min, sensor_humi_max;
    double sensor_pearson_corr;
    double sensor_thi_mean;
    char comfort_grade[20];
    char comfort_suggestion[40];
    int temp_anom_3sigma, humi_anom_3sigma;
    double temp_trend_per_day, humi_trend_per_day;

    // 天气统计（外部天气数据）
    int weather_count;
    double weather_temp_mean, weather_temp_std, weather_temp_min, weather_temp_max;
    double weather_humi_mean, weather_humi_std, weather_humi_min, weather_humi_max;
    char most_common_weather[30];

    // 对比指标
    double temp_diff_mean;          // 传感器温度 - 天气温度 平均值
    double temp_corr_with_weather;  // 传感器温度与天气温度的相关性
    double humi_diff_mean;          // 传感器湿度 - 天气湿度
    double humi_corr_with_weather;
} report_analysis_t;

// 初始化邮件模块（收件人，发送时间小时，分钟）
void email_report_init(const char *recipient, int hour, int minute);

// 邮件线程函数（独立运行，每分钟检查一次）
void *email_report_thread(void *arg);

// 手动触发发送当日报告（可用于测试）
void email_send_daily_report(void);
/* 独立测试函数 - 直接发送邮件，不依赖数据库 */
void email_test_send(void);   // 快速测试邮件发送

#endif