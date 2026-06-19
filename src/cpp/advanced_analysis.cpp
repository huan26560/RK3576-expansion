#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include <numeric>
#include <map>
#include "c_api_wrapper.h"


// ==================== 数据结构 ====================
typedef struct {
    // 传感器统计
    int sensor_count;
    double temp_mean, temp_std, temp_min, temp_max, temp_median;
    double humi_mean, humi_std, humi_min, humi_max, humi_median;
    double temp_skewness, temp_kurtosis;
    double humi_skewness, humi_kurtosis;
    double temp_range, humi_range;
    double temp_cv, humi_cv;  // 变异系数
    
    // 天气统计
    int weather_count;
    double weather_temp_mean, weather_temp_std, weather_temp_min, weather_temp_max;
    double weather_humi_mean, weather_humi_std, weather_humi_min, weather_humi_max;
    
    // 趋势分析
    double temp_trend, humi_trend;
    double weather_temp_trend, weather_humi_trend;
    double temp_acceleration, humi_acceleration;  // 加速度（二阶趋势）
    
    // 相关性分析
    double temp_corr_pearson, temp_corr_spearman;
    double humi_corr_pearson, humi_corr_spearman;
    double temp_humi_corr_pearson;  // 传感器温湿度相关性
    
    // 室内外对比
    double avg_temp_diff, avg_humi_diff;
    double temp_diff_mean, temp_diff_std;  // 添加这两个字段
    double temp_diff_max, temp_diff_min;
    
    // 异常检测
    int temp_anomalies_3sigma, humi_anomalies_3sigma;
    int weather_temp_anomalies, weather_humi_anomalies;
    double temp_anomaly_ratio, humi_anomaly_ratio;
    
    // 极端事件
    int hot_days, cold_days, high_humi_days, low_humi_days;
    int rainy_days, clear_days, cloudy_days, foggy_days;
    int extreme_temp_events, extreme_humi_events;
    
    // 舒适度
    double thi_mean, thi_min, thi_max, thi_std;
    double heat_index_mean;  // 热指数
    char grade[32];
    char comfort_level[32];
    
    // 周期性分析
    double diurnal_temp_amplitude;  // 日温差幅度
    double diurnal_humi_amplitude;
    
    // 综合评分
    double overall_score;
    char summary[512];
    char suggestions[512];
} analysis_result_t;

// ==================== 基础统计 ====================
static double calc_mean(const double* data, int n) {
    if (n <= 0) return 0;
    double sum = 0;
    for (int i = 0; i < n; i++) sum += data[i];
    return sum / n;
}

static double calc_median(std::vector<double> data) {
    if (data.empty()) return 0;
    std::sort(data.begin(), data.end());
    size_t n = data.size();
    return n % 2 ? data[n/2] : (data[n/2 - 1] + data[n/2]) / 2.0;
}

static double calc_std(const double* data, int n, double mean) {
    if (n <= 1) return 0;
    double sum = 0;
    for (int i = 0; i < n; i++) sum += (data[i] - mean) * (data[i] - mean);
    return sqrt(sum / (n - 1));
}

static double calc_skewness(const double* data, int n, double mean, double std) {
    if (n < 3 || std == 0) return 0;
    double sum = 0;
    for (int i = 0; i < n; i++) {
        double z = (data[i] - mean) / std;
        sum += z * z * z;
    }
    return sum / n;
}

static double calc_kurtosis(const double* data, int n, double mean, double std) {
    if (n < 4 || std == 0) return 0;
    double sum = 0;
    for (int i = 0; i < n; i++) {
        double z = (data[i] - mean) / std;
        sum += z * z * z * z;
    }
    return sum / n - 3.0;
}

static double calc_pearson(const double* x, const double* y, int n) {
    if (n < 2) return 0;
    double mx = calc_mean(x, n);
    double my = calc_mean(y, n);
    double num = 0, dx = 0, dy = 0;
    for (int i = 0; i < n; i++) {
        num += (x[i] - mx) * (y[i] - my);
        dx += (x[i] - mx) * (x[i] - mx);
        dy += (y[i] - my) * (y[i] - my);
    }
    return dx * dy != 0 ? num / sqrt(dx * dy) : 0;
}

static double calc_spearman(const double* x, const double* y, int n) {
    if (n < 2) return 0;
    std::vector<double> sx(x, x + n), sy(y, y + n);
    std::sort(sx.begin(), sx.end());
    std::sort(sy.begin(), sy.end());
    
    std::vector<double> rx(n), ry(n);
    for (int i = 0; i < n; i++) {
        rx[i] = std::lower_bound(sx.begin(), sx.end(), x[i]) - sx.begin() + 1;
        ry[i] = std::lower_bound(sy.begin(), sy.end(), y[i]) - sy.begin() + 1;
    }
    return calc_pearson(rx.data(), ry.data(), n);
}

static double calc_trend(const double* y, int n) {
    if (n < 2) return 0;
    double sx = 0, sy = 0, sxy = 0, sx2 = 0;
    for (int i = 0; i < n; i++) {
        double xi = i;
        sx += xi;
        sy += y[i];
        sxy += xi * y[i];
        sx2 += xi * xi;
    }
    double denom = n * sx2 - sx * sx;
    return denom != 0 ? (n * sxy - sx * sy) / denom : 0;
}

static double calc_acceleration(const double* y, int n) {
    // 二阶趋势（加速度）
    if (n < 3) return 0;
    std::vector<double> y_smooth;
    for (int i = 1; i < n - 1; i++) {
        y_smooth.push_back((y[i-1] + y[i] + y[i+1]) / 3.0);
    }
    return calc_trend(y_smooth.data(), y_smooth.size());
}

static double calc_cv(double mean, double std) {
    return mean != 0 ? (std / mean) * 100 : 0;
}

static double calc_heat_index(double temp, double humi) {
    // 简化版热指数
    if (temp < 27) return temp;
    double hi = -42.379 + 2.04901523 * temp + 10.14333127 * humi
              - 0.22475541 * temp * humi - 0.00683783 * temp * temp
              - 0.05481717 * humi * humi + 0.00122874 * temp * temp * humi
              + 0.00085282 * temp * humi * humi - 0.00000199 * temp * temp * humi * humi;
    return hi;
}

// ==================== 主分析函数 ====================
extern "C" int perform_advanced_analysis(char* buffer, int buf_size) {
    analysis_result_t res = {0};
    
    // 1. 加载传感器数据
    double *sensor_temp = NULL, *sensor_humi = NULL, *days = NULL;
    int sensor_count = 0;
    if (db_get_sensor_analysis_data(&sensor_temp, &sensor_humi, &days, &sensor_count) != 0 || sensor_count == 0) {
        snprintf(buffer, buf_size, "No sensor data");
        return -1;
    }
    res.sensor_count = sensor_count;
    
    // 2. 加载天气数据（全部数据）
    time_t now = time(NULL);
    time_t start_ts = 0;
    double *weather_temp = NULL, *weather_humi = NULL;
    char **weather_desc = NULL;
    int weather_count = 0;
    db_get_weather_data_range(start_ts, now, &weather_temp, &weather_humi, &weather_desc, &weather_count);
    res.weather_count = weather_count;
    
    // ========== 3. 传感器统计分析 ==========
    res.temp_mean = calc_mean(sensor_temp, sensor_count);
    res.temp_std = calc_std(sensor_temp, sensor_count, res.temp_mean);
    res.humi_mean = calc_mean(sensor_humi, sensor_count);
    res.humi_std = calc_std(sensor_humi, sensor_count, res.humi_mean);
    
    // 中位数
    std::vector<double> temp_vec(sensor_temp, sensor_temp + sensor_count);
    std::vector<double> humi_vec(sensor_humi, sensor_humi + sensor_count);
    res.temp_median = calc_median(temp_vec);
    res.humi_median = calc_median(humi_vec);
    
    // 极值
    res.temp_min = sensor_temp[0];
    res.temp_max = sensor_temp[0];
    res.humi_min = sensor_humi[0];
    res.humi_max = sensor_humi[0];
    for (int i = 1; i < sensor_count; i++) {
        if (sensor_temp[i] < res.temp_min) res.temp_min = sensor_temp[i];
        if (sensor_temp[i] > res.temp_max) res.temp_max = sensor_temp[i];
        if (sensor_humi[i] < res.humi_min) res.humi_min = sensor_humi[i];
        if (sensor_humi[i] > res.humi_max) res.humi_max = sensor_humi[i];
    }
    res.temp_range = res.temp_max - res.temp_min;
    res.humi_range = res.humi_max - res.humi_min;
    
    // 偏度和峰度
    res.temp_skewness = calc_skewness(sensor_temp, sensor_count, res.temp_mean, res.temp_std);
    res.temp_kurtosis = calc_kurtosis(sensor_temp, sensor_count, res.temp_mean, res.temp_std);
    res.humi_skewness = calc_skewness(sensor_humi, sensor_count, res.humi_mean, res.humi_std);
    res.humi_kurtosis = calc_kurtosis(sensor_humi, sensor_count, res.humi_mean, res.humi_std);
    
    // 变异系数
    res.temp_cv = calc_cv(res.temp_mean, res.temp_std);
    res.humi_cv = calc_cv(res.humi_mean, res.humi_std);
    
    // 传感器温湿度相关性
    res.temp_humi_corr_pearson = calc_pearson(sensor_temp, sensor_humi, sensor_count);
    
    // 趋势和加速度
    res.temp_trend = calc_trend(sensor_temp, sensor_count);
    res.humi_trend = calc_trend(sensor_humi, sensor_count);
    res.temp_acceleration = calc_acceleration(sensor_temp, sensor_count);
    res.humi_acceleration = calc_acceleration(sensor_humi, sensor_count);
    
    // ========== 4. 天气统计分析 ==========
    if (weather_count > 0) {
        res.weather_temp_mean = calc_mean(weather_temp, weather_count);
        res.weather_temp_std = calc_std(weather_temp, weather_count, res.weather_temp_mean);
        res.weather_humi_mean = calc_mean(weather_humi, weather_count);
        res.weather_humi_std = calc_std(weather_humi, weather_count, res.weather_humi_mean);
        
        res.weather_temp_min = weather_temp[0];
        res.weather_temp_max = weather_temp[0];
        res.weather_humi_min = weather_humi[0];
        res.weather_humi_max = weather_humi[0];
        for (int i = 1; i < weather_count; i++) {
            if (weather_temp[i] < res.weather_temp_min) res.weather_temp_min = weather_temp[i];
            if (weather_temp[i] > res.weather_temp_max) res.weather_temp_max = weather_temp[i];
            if (weather_humi[i] < res.weather_humi_min) res.weather_humi_min = weather_humi[i];
            if (weather_humi[i] > res.weather_humi_max) res.weather_humi_max = weather_humi[i];
        }
        
        res.weather_temp_trend = calc_trend(weather_temp, weather_count);
        res.weather_humi_trend = calc_trend(weather_humi, weather_count);
        
        // 天气统计
        for (int i = 0; i < weather_count; i++) {
            if (weather_desc[i]) {
                if (strstr(weather_desc[i], "Rain")) res.rainy_days++;
                else if (strstr(weather_desc[i], "Clear")) res.clear_days++;
                else if (strstr(weather_desc[i], "Cloudy")) res.cloudy_days++;
                else if (strstr(weather_desc[i], "Fog")) res.foggy_days++;
            }
        }
    }
    
    // ========== 5. 室内外对比 ==========
    if (weather_count > 0) {
        res.avg_temp_diff = res.temp_mean - res.weather_temp_mean;
        res.avg_humi_diff = res.humi_mean - res.weather_humi_mean;
        
        // 温差标准差
        std::vector<double> temp_diffs;
        int min_count = sensor_count < weather_count ? sensor_count : weather_count;
        for (int i = 0; i < min_count; i++) {
            temp_diffs.push_back(sensor_temp[i] - weather_temp[i]);
        }
        res.temp_diff_mean = calc_mean(temp_diffs.data(), temp_diffs.size());
        res.temp_diff_std = calc_std(temp_diffs.data(), temp_diffs.size(), res.temp_diff_mean);
        
        // 相关性
        res.temp_corr_pearson = calc_pearson(sensor_temp, weather_temp, min_count);
        res.temp_corr_spearman = calc_spearman(sensor_temp, weather_temp, min_count);
        res.humi_corr_pearson = calc_pearson(sensor_humi, weather_humi, min_count);
        res.humi_corr_spearman = calc_spearman(sensor_humi, weather_humi, min_count);
    }
    
    // ========== 6. 异常检测 ==========
    double temp_3sigma = 3 * res.temp_std;
    double humi_3sigma = 3 * res.humi_std;
    for (int i = 0; i < sensor_count; i++) {
        if (fabs(sensor_temp[i] - res.temp_mean) > temp_3sigma) res.temp_anomalies_3sigma++;
        if (fabs(sensor_humi[i] - res.humi_mean) > humi_3sigma) res.humi_anomalies_3sigma++;
    }
    res.temp_anomaly_ratio = (double)res.temp_anomalies_3sigma / sensor_count * 100;
    res.humi_anomaly_ratio = (double)res.humi_anomalies_3sigma / sensor_count * 100;
    
    // ========== 7. 极端事件 ==========
    for (int i = 0; i < sensor_count; i++) {
        if (sensor_temp[i] > 35.0) res.hot_days++;
        if (sensor_temp[i] < 0.0) res.cold_days++;
        if (sensor_humi[i] > 80.0) res.high_humi_days++;
        if (sensor_humi[i] < 30.0) res.low_humi_days++;
    }
    res.extreme_temp_events = res.hot_days + res.cold_days;
    res.extreme_humi_events = res.high_humi_days + res.low_humi_days;
    
    // ========== 8. 舒适度分析 ==========
    double thi_sum = 0, thi_min = 100, thi_max = -100;
    double hi_sum = 0;
    for (int i = 0; i < sensor_count; i++) {
        double thi = 0.8 * sensor_temp[i] + (0.01 * sensor_humi[i]) * (0.8 * sensor_temp[i] - 14.3) + 46.3;
        thi_sum += thi;
        if (thi < thi_min) thi_min = thi;
        if (thi > thi_max) thi_max = thi;
        
        double hi = calc_heat_index(sensor_temp[i], sensor_humi[i]);
        hi_sum += hi;
    }
    res.thi_mean = thi_sum / sensor_count;
    res.thi_min = thi_min;
    res.thi_max = thi_max;
    res.thi_std = calc_std(&thi_sum, 1, res.thi_mean);  // 简化
    res.heat_index_mean = hi_sum / sensor_count;
    
    // 舒适度等级
    if (res.thi_mean < 55) {
        strcpy(res.grade, "Cold");
        strcpy(res.comfort_level, "Uncomfortable");
    } else if (res.thi_mean < 60) {
        strcpy(res.grade, "Cool");
        strcpy(res.comfort_level, "Slightly Cool");
    } else if (res.thi_mean < 65) {
        strcpy(res.grade, "Comfort");
        strcpy(res.comfort_level, "Comfortable");
    } else if (res.thi_mean < 70) {
        strcpy(res.grade, "Warm");
        strcpy(res.comfort_level, "Slightly Warm");
    } else if (res.thi_mean < 75) {
        strcpy(res.grade, "Hot");
        strcpy(res.comfort_level, "Uncomfortable");
    } else {
        strcpy(res.grade, "Muggy");
        strcpy(res.comfort_level, "Very Uncomfortable");
    }
    
    // ========== 9. 周期性分析 ==========
    // 计算日温差幅度（简化：取最高温-最低温）
    res.diurnal_temp_amplitude = res.temp_max - res.temp_min;
    res.diurnal_humi_amplitude = res.humi_max - res.humi_min;
    
    // ========== 10. 综合评分 ==========
    double score = 100;
    // 温度扣分
    if (res.temp_mean < 18 || res.temp_mean > 28) score -= 15;
    // 湿度扣分
    if (res.humi_mean < 40 || res.humi_mean > 70) score -= 10;
    // 异常扣分
    score -= res.temp_anomaly_ratio * 0.3;
    score -= res.humi_anomaly_ratio * 0.3;
    // 极端事件扣分
    score -= res.extreme_temp_events * 2;
    score -= res.extreme_humi_events * 1;
    // 趋势扣分（上升太快不好）
    if (fabs(res.temp_trend) > 0.5) score -= 5;
    if (fabs(res.humi_trend) > 1.0) score -= 5;
    
    res.overall_score = score < 0 ? 0 : (score > 100 ? 100 : score);
    
// ========== 11. 生成报告（无空格满屏版） ==========
    int off = 0;
    
    off += snprintf(buffer + off, buf_size - off,
        "===Advanced Analysis===\n");
    off += snprintf(buffer + off, buf_size - off,
        "[1]Indoor Sensor\n");
    off += snprintf(buffer + off, buf_size - off,
        "N:%-4d T:%.1f+-%.1f\n", sensor_count, res.temp_mean, res.temp_std);
    off += snprintf(buffer + off, buf_size - off,
        "T:[%.1f-%.1f] H:%.1f+-%.1f\n", res.temp_min, res.temp_max, res.humi_mean, res.humi_std);
    off += snprintf(buffer + off, buf_size - off,
        "H:[%.0f-%.0f] Med:T%.1f\n", res.humi_min, res.humi_max, res.temp_median);
    off += snprintf(buffer + off, buf_size - off,
        "Med:H%.1f Skew:T%.2f\n", res.humi_median, res.temp_skewness);
    off += snprintf(buffer + off, buf_size - off,
        "Skew:H%.2f CV:T%.1f%%\n", res.humi_skewness, res.temp_cv);
    off += snprintf(buffer + off, buf_size - off,
        "CV:H%.1f%%\n", res.humi_cv);
    
    off += snprintf(buffer + off, buf_size - off,
        "[2]Trend\n");
    off += snprintf(buffer + off, buf_size - off,
        "T:%+.3fC/d H:%+.3f%%/d\n", res.temp_trend, res.humi_trend);
    off += snprintf(buffer + off, buf_size - off,
        "Accel:T%+.3f H%+.3f\n", res.temp_acceleration, res.humi_acceleration);
    off += snprintf(buffer + off, buf_size - off,
        "T-H Corr:%.3f\n", res.temp_humi_corr_pearson);
    
    if (weather_count > 0) {
        off += snprintf(buffer + off, buf_size - off,
            "[3]Outdoor Weather\n");
        off += snprintf(buffer + off, buf_size - off,
            "N:%-4d T:%.1f+-%.1f\n", weather_count, res.weather_temp_mean, res.weather_temp_std);
        off += snprintf(buffer + off, buf_size - off,
            "H:%.1f+-%.1f%% [%.0f-%.0f]\n", res.weather_humi_mean, res.weather_humi_std, res.weather_humi_min, res.weather_humi_max);
        off += snprintf(buffer + off, buf_size - off,
            "Rain:%-3d Clear:%-3d\n", res.rainy_days, res.clear_days);
        off += snprintf(buffer + off, buf_size - off,
            "Cloudy:%-3d\n", res.cloudy_days);
    }
    
    if (weather_count > 0) {
        off += snprintf(buffer + off, buf_size - off,
            "[4]In-Out Comp\n");
        off += snprintf(buffer + off, buf_size - off,
            "Td:%+.1fC Hd:%+.1f%%\n", res.temp_diff_mean, res.avg_humi_diff);
        off += snprintf(buffer + off, buf_size - off,
            "Tcorr:%.3f(P) %.3f(S)\n", res.temp_corr_pearson, res.temp_corr_spearman);
        off += snprintf(buffer + off, buf_size - off,
            "Hcorr:%.3f(P) %.3f(S)\n", res.humi_corr_pearson, res.humi_corr_spearman);
    }
    
    off += snprintf(buffer + off, buf_size - off,
        "[5]Anomaly\n");
    off += snprintf(buffer + off, buf_size - off,
        "T:%d(%.1f%%) H:%d(%.1f%%)\n", res.temp_anomalies_3sigma, res.temp_anomaly_ratio, res.humi_anomalies_3sigma, res.humi_anomaly_ratio);
    
    off += snprintf(buffer + off, buf_size - off,
        "[6]Extreme\n");
    off += snprintf(buffer + off, buf_size - off,
        "Hot:%-3d Cold:%-3d\n", res.hot_days, res.cold_days);
    off += snprintf(buffer + off, buf_size - off,
        "H-Hi:%-3d H-Lo:%-3d\n", res.high_humi_days, res.low_humi_days);
    off += snprintf(buffer + off, buf_size - off,
        "T-Ev:%d H-Ev:%d\n", res.extreme_temp_events, res.extreme_humi_events);
    
    off += snprintf(buffer + off, buf_size - off,
        "[7]Comfort\n");
    off += snprintf(buffer + off, buf_size - off,
        "THI:%.1f[%.1f-%.1f]\n", res.thi_mean, res.thi_min, res.thi_max);
    off += snprintf(buffer + off, buf_size - off,
        "HeatIdx:%.1f\n", res.heat_index_mean);
    off += snprintf(buffer + off, buf_size - off,
        "Grade:%s\n", res.grade);
    off += snprintf(buffer + off, buf_size - off,
        "Amp:T%.1f H%.1f%%\n", res.diurnal_temp_amplitude, res.diurnal_humi_amplitude);
    
    off += snprintf(buffer + off, buf_size - off,
        "[8]Score\n");
    off += snprintf(buffer + off, buf_size - off,
        "%.1f/100 %s\n", res.overall_score, 
        res.overall_score >= 80 ? "Excellent" : (res.overall_score >= 60 ? "Good" : "Poor"));
    
    off += snprintf(buffer + off, buf_size - off,
        "[9]Suggest\n");
    if (res.avg_temp_diff > 2) {
        off += snprintf(buffer + off, buf_size - off,
            "Ventilate\n");
    }
    if (res.thi_mean > 70) {
        off += snprintf(buffer + off, buf_size - off,
            "Use AC/Fan\n");
    }
    if (res.high_humi_days > sensor_count / 2) {
        off += snprintf(buffer + off, buf_size - off,
            "Dehumidify\n");
    }
    if (res.temp_anomaly_ratio > 10) {
        off += snprintf(buffer + off, buf_size - off,
            "Monitor temp\n");
    }
    if (res.temp_trend > 0.3) {
        off += snprintf(buffer + off, buf_size - off,
            "Rising temp\n");
    }
    if (res.temp_corr_pearson > 0.7) {
        off += snprintf(buffer + off, buf_size - off,
            "Follows outdoor\n");
    }
    if (res.overall_score < 60) {
        off += snprintf(buffer + off, buf_size - off,
            "Take action!\n");
    }
    
    // 清理内存
    free(sensor_temp);
    free(sensor_humi);
    free(days);
    free(weather_temp);
    free(weather_humi);
    for (int i = 0; i < weather_count; i++) free(weather_desc[i]);
    free(weather_desc);
    
    return 0;
}