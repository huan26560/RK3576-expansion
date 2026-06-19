#ifndef DB_HELPER_H
#define DB_HELPER_H

typedef struct
{
    char name[32];
    char desc[32];
    int count;
} db_table_info_t;

typedef struct
{
    int id;
    float temp;
    float humi;
    char ts[16]; // MM-DD HH:MM
} db_preview_row_t;

int db_init(void);
void db_save_dht11(float temp, float humi);
int db_get_table_list(db_table_info_t *out, int max_count);
int db_get_preview(const char *tablename, int offset, db_preview_row_t *out, int max_rows);
int db_export_xlsx(const char *tablename, const char *filepath, char *msg, int msg_len);
/* 获取传感器数据，返回天数（相对时间） */
int db_get_sensor_analysis_data(double **temp, double **humi, double **days, int *count);
// 在 db_helper.h 中添加
void db_save_weather(float temp, float humi, const char *location, const char *weather_desc);
int db_create_weather_table(void); // 可选，用于建表，但建议在 db_init 中处理
// 获取指定时间范围内的传感器数据（返回动态数组，需释放）
int db_get_sensor_data_range(time_t start_ts, time_t end_ts,
                             double **temp_out, double **humi_out, int *count);

// 获取指定时间范围内的天气数据
int db_get_weather_data_range(time_t start_ts, time_t end_ts,
                              double **temp_out, double **humi_out, char ***desc_out, int *count);
void db_close(void);

#endif