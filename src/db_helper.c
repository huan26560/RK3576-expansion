#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mysql/mysql.h>
#include <xlsxwriter.h>
#include "db_helper.h"

static MYSQL *db_conn = NULL;

int db_init(void)
{
    if (db_conn != NULL)
        return 0;

    db_conn = mysql_init(NULL);
    if (db_conn == NULL)
    {
        fprintf(stderr, "[DB] mysql_init failed\n");
        return -1;
    }

    if (mysql_real_connect(db_conn, "127.0.0.1", "admin", "19981009huan",
                           "mydb", 3306, NULL, 0) == NULL)
    {
        fprintf(stderr, "[DB] Connect failed: %s\n", mysql_error(db_conn));
        mysql_close(db_conn);
        db_conn = NULL;
        return -1;
    }

    // ---- 建表（确保表存在） ----
    const char *create_sensor =
        "CREATE TABLE IF NOT EXISTS sensor_data ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "ts TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
        "temp FLOAT, "
        "humi FLOAT)";

    const char *create_weather =
        "CREATE TABLE IF NOT EXISTS weather ("
        "id INT AUTO_INCREMENT PRIMARY KEY, "
        "ts TIMESTAMP DEFAULT CURRENT_TIMESTAMP, "
        "temperature FLOAT, "
        "humidity FLOAT, "
        "location VARCHAR(100), "
        "weather_desc VARCHAR(50))";

    if (mysql_query(db_conn, create_sensor))
    {
        fprintf(stderr, "[DB] Create sensor table failed: %s\n", mysql_error(db_conn));
        // 不致命，继续
    }
    if (mysql_query(db_conn, create_weather))
    {
        fprintf(stderr, "[DB] Create weather table failed: %s\n", mysql_error(db_conn));
        // 不致命，继续
    }

    printf("[DB] Connected to MySQL and tables ready\n");
    return 0;
}

void db_save_dht11(float temp, float humi)
{
    if (db_conn == NULL)
    {
        if (db_init() != 0)
            return;
    }

    char sql[256];
    snprintf(sql, sizeof(sql),
             "INSERT INTO sensor_data (temp, humi) VALUES (%.1f, %.1f)",
             temp, humi);

    if (mysql_query(db_conn, sql))
    {
        fprintf(stderr, "[DB] INSERT failed: %s\n", mysql_error(db_conn));

        mysql_close(db_conn);
        db_conn = NULL;
        if (db_init() == 0)
        {
            if (mysql_query(db_conn, sql) == 0)
            {
                printf("[DB] Saved temp=%.1f, humi=%.1f (reconnect)\n", temp, humi);
            }
        }
    }
    else
    {
        printf("[DB] Saved temp=%.1f, humi=%.1f\n", temp, humi);
    }
}

void db_close(void)
{
    if (db_conn)
    {
        mysql_close(db_conn);
        db_conn = NULL;
        printf("[DB] Connection closed\n");
    }
}

/* ========== 获取表列表（供UI显示） ========== */

int db_get_table_list(db_table_info_t *out, int max_count)
{
    if (db_conn == NULL && db_init() != 0)
        return 0;

    /* 查询所有用户表（排除系统表） */
    if (mysql_query(db_conn,
                    "SELECT table_name FROM information_schema.tables "
                    "WHERE table_schema='mydb' AND table_type='BASE TABLE'"))
    {
        return 0;
    }

    MYSQL_RES *res = mysql_store_result(db_conn);
    if (!res)
        return 0;

    int i = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) && i < max_count)
    {
        const char *name = row[0];

        /* 跳过系统表 */
        if (strcmp(name, "tables_priv") == 0 ||
            strcmp(name, "columns_priv") == 0 ||
            strcmp(name, "db") == 0 ||
            strcmp(name, "user") == 0)
        {
            continue;
        }

        strncpy(out[i].name, name, sizeof(out[i].name) - 1);

        /* 显示名称：sensor_data → "Sensor Data" */
        if (strcmp(name, "sensor_data") == 0)
        {
            strcpy(out[i].desc, "Sensor Data");
        }
        else
        {
            strncpy(out[i].desc, name, sizeof(out[i].desc) - 1);
        }

        /* 查记录数 */
        char cnt_sql[128];
        snprintf(cnt_sql, sizeof(cnt_sql), "SELECT COUNT(*) FROM `%s`", name);
        if (mysql_query(db_conn, cnt_sql) == 0)
        {
            MYSQL_RES *cnt_res = mysql_store_result(db_conn);
            if (cnt_res)
            {
                MYSQL_ROW cnt_row = mysql_fetch_row(cnt_res);
                out[i].count = cnt_row ? atoi(cnt_row[0]) : 0;
                mysql_free_result(cnt_res);
            }
        }

        i++;
    }

    mysql_free_result(res);
    return i;
}

/* ========== 导出指定表为 xlsx ========== */

int db_export_xlsx(const char *tablename, const char *filepath, char *msg, int msg_len)
{
    if (db_conn == NULL && db_init() != 0)
    {
        snprintf(msg, msg_len, "DB Error");
        return -1;
    }

    /* 获取表的所有列名 */
    char cols_sql[256];
    snprintf(cols_sql, sizeof(cols_sql),
             "SELECT COLUMN_NAME FROM information_schema.COLUMNS "
             "WHERE TABLE_SCHEMA='mydb' AND TABLE_NAME='%s' ORDER BY ORDINAL_POSITION",
             tablename);

    if (mysql_query(db_conn, cols_sql))
    {
        snprintf(msg, msg_len, "Get cols failed");
        return -1;
    }

    MYSQL_RES *cols_res = mysql_store_result(db_conn);
    if (!cols_res)
    {
        snprintf(msg, msg_len, "No columns");
        return -1;
    }

    char col_names[32][32];
    int col_count = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(cols_res)) && col_count < 32)
    {
        strncpy(col_names[col_count], row[0], 31);
        col_count++;
    }
    mysql_free_result(cols_res);

    if (col_count == 0)
    {
        snprintf(msg, msg_len, "Empty table");
        return -1;
    }

    /* 创建 xlsx */
    lxw_workbook *workbook = workbook_new(filepath);
    if (!workbook)
    {
        snprintf(msg, msg_len, "File Err");
        return -1;
    }

    lxw_worksheet *worksheet = workbook_add_worksheet(workbook, tablename);

    /* 写表头 */
    for (int c = 0; c < col_count; c++)
    {
        worksheet_write_string(worksheet, 0, c, col_names[c], NULL);
    }

    /* 查数据 */
    char data_sql[256];
    snprintf(data_sql, sizeof(data_sql), "SELECT * FROM `%s`", tablename);

    if (mysql_query(db_conn, data_sql))
    {
        snprintf(msg, msg_len, "Query Err");
        workbook_close(workbook);
        return -1;
    }

    MYSQL_RES *res = mysql_store_result(db_conn);
    if (!res)
    {
        snprintf(msg, msg_len, "No Data");
        workbook_close(workbook);
        return -1;
    }

    int data_row = 1;
    MYSQL_ROW r;
    while ((r = mysql_fetch_row(res)))
    {
        for (int c = 0; c < col_count; c++)
        {
            /* 尝试数字，失败写字符串 */
            char *endptr;
            double val = strtod(r[c], &endptr);
            if (*endptr == '\0' && r[c][0] != '\0')
            {
                worksheet_write_number(worksheet, data_row, c, val, NULL);
            }
            else
            {
                worksheet_write_string(worksheet, data_row, c, r[c] ? r[c] : "", NULL);
            }
        }
        data_row++;
    }

    mysql_free_result(res);

    if (workbook_close(workbook) != 0)
    {
        snprintf(msg, msg_len, "Write Err");
        return -1;
    }

    snprintf(msg, msg_len, "OK %d rows", data_row - 1);
    return 0;
}


int db_get_preview(const char *tablename, int offset, db_preview_row_t *out, int max_rows)
{
    if (db_conn == NULL && db_init() != 0)
        return 0;

    char sql[512];

    // 根据表名选择正确的字段映射
    if (strcmp(tablename, "sensor_data") == 0) {
        snprintf(sql, sizeof(sql),
                 "SELECT id, temp, humi, DATE_FORMAT(ts, '%%m-%%d %%H:%%i') "
                 "FROM `%s` ORDER BY id DESC LIMIT %d OFFSET %d",
                 tablename, max_rows, offset);
    } else if (strcmp(tablename, "weather") == 0) {
        // weather 表字段：temperature, humidity, ts
        snprintf(sql, sizeof(sql),
                 "SELECT id, temperature AS temp, humidity AS humi, DATE_FORMAT(ts, '%%m-%%d %%H:%%i') "
                 "FROM `%s` ORDER BY id DESC LIMIT %d OFFSET %d",
                 tablename, max_rows, offset);
    } else {
        // 其他表尝试通用查询（假设存在 temp, humi, ts）
        snprintf(sql, sizeof(sql),
                 "SELECT id, temp, humi, DATE_FORMAT(ts, '%%m-%%d %%H:%%i') "
                 "FROM `%s` ORDER BY id DESC LIMIT %d OFFSET %d",
                 tablename, max_rows, offset);
    }

    if (mysql_query(db_conn, sql))
    {
        fprintf(stderr, "[DB] Preview query failed: %s\n", mysql_error(db_conn));
        return 0;
    }

    MYSQL_RES *res = mysql_store_result(db_conn);
    if (!res)
        return 0;

    int i = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) && i < max_rows)
    {
        out[i].id = atoi(row[0]);
        out[i].temp = atof(row[1]);
        out[i].humi = atof(row[2]);
        strncpy(out[i].ts, row[3], sizeof(out[i].ts) - 1);
        i++;
    }
    mysql_free_result(res);
    return i;
}

int db_get_sensor_analysis_data(double **temp, double **humi, double **days, int *count)
{
    if (db_conn == NULL && db_init() != 0)
        return -1;

    // 获取最早的时间戳作为基准
    const char *sql_min = "SELECT UNIX_TIMESTAMP(MIN(ts)) FROM sensor_data WHERE temp IS NOT NULL AND humi IS NOT NULL";
    if (mysql_query(db_conn, sql_min))
        return -1;
    MYSQL_RES *res_min = mysql_store_result(db_conn);
    if (!res_min)
        return -1;
    MYSQL_ROW row_min = mysql_fetch_row(res_min);
    double t0 = atof(row_min[0]);
    mysql_free_result(res_min);

    // 获取所有数据，时间戳转换为相对天数
    char sql[512];
    snprintf(sql, sizeof(sql),
             "SELECT temp, humi, (UNIX_TIMESTAMP(ts) - %f) / 86400.0 FROM sensor_data "
             "WHERE temp IS NOT NULL AND humi IS NOT NULL ORDER BY ts ASC",
             t0);

    if (mysql_query(db_conn, sql))
    {
        fprintf(stderr, "[DB] Query failed: %s\n", mysql_error(db_conn));
        return -1;
    }

    MYSQL_RES *res = mysql_store_result(db_conn);
    if (!res)
        return -1;

    int n = (int)mysql_num_rows(res);
    if (n == 0)
    {
        mysql_free_result(res);
        return -1;
    }

    double *t = (double *)malloc(n * sizeof(double));
    double *h = (double *)malloc(n * sizeof(double));
    double *d = (double *)malloc(n * sizeof(double));
    if (!t || !h || !d)
    {
        free(t);
        free(h);
        free(d);
        mysql_free_result(res);
        return -1;
    }

    MYSQL_ROW row;
    int i = 0;
    while ((row = mysql_fetch_row(res)) && i < n)
    {
        t[i] = atof(row[0]);
        h[i] = atof(row[1]);
        d[i] = atof(row[2]); // 已经是相对天数（0 ~ 21天）
        i++;
    }
    mysql_free_result(res);

    *temp = t;
    *humi = h;
    *days = d;
    *count = n;
    return 0;
}
void db_save_weather(float temp, float humi, const char *location, const char *weather_desc)
{
    if (db_conn == NULL)
    {
        if (db_init() != 0)
            return;
    }

    char sql[512];
    snprintf(sql, sizeof(sql),
             "INSERT INTO weather (temperature, humidity, location, weather_desc) "
             "VALUES (%.1f, %.1f, '%s', '%s')",
             temp, humi, location, weather_desc);

    if (mysql_query(db_conn, sql))
    {
        fprintf(stderr, "[DB] Weather INSERT failed: %s\n", mysql_error(db_conn));
        // 尝试重连重试
        mysql_close(db_conn);
        db_conn = NULL;
        if (db_init() == 0)
        {
            if (mysql_query(db_conn, sql) == 0)
            {
                printf("[DB] Weather saved (reconnect) temp=%.1f humi=%.1f\n", temp, humi);
            }
            else
            {
                fprintf(stderr, "[DB] Weather retry failed\n");
            }
        }
    }
    else
    {
        printf("[DB] Weather saved temp=%.1f humi=%.1f loc=%s\n", temp, humi, location);
    }
}
int db_get_sensor_data_range(time_t start_ts, time_t end_ts,
                             double **temp_out, double **humi_out, int *count)
{
    if (db_conn == NULL && db_init() != 0)
        return -1;

    const char *sql =
        "SELECT temp, humi FROM sensor_data "
        "WHERE UNIX_TIMESTAMP(ts) >= %ld AND UNIX_TIMESTAMP(ts) <= %ld "
        "ORDER BY ts ASC";
    char query[512];
    snprintf(query, sizeof(query), sql, start_ts, end_ts);

    if (mysql_query(db_conn, query))
        return -1;
    MYSQL_RES *res = mysql_store_result(db_conn);
    if (!res)
        return -1;

    int n = mysql_num_rows(res);
    if (n == 0)
    {
        mysql_free_result(res);
        *count = 0;
        return 0;
    }

    double *t = malloc(n * sizeof(double));
    double *h = malloc(n * sizeof(double));
    if (!t || !h)
    {
        free(t);
        free(h);
        mysql_free_result(res);
        return -1;
    }

    MYSQL_ROW row;
    int i = 0;
    while ((row = mysql_fetch_row(res)) && i < n)
    {
        t[i] = atof(row[0]);
        h[i] = atof(row[1]);
        i++;
    }
    mysql_free_result(res);

    *temp_out = t;
    *humi_out = h;
    *count = n;
    return 0;
}

int db_get_weather_data_range(time_t start_ts, time_t end_ts,
                              double **temp_out, double **humi_out, char ***desc_out, int *count)
{
    if (db_conn == NULL && db_init() != 0)
        return -1;

    const char *sql =
        "SELECT temperature, humidity, weather_desc FROM weather "
        "WHERE UNIX_TIMESTAMP(ts) >= %ld AND UNIX_TIMESTAMP(ts) <= %ld "
        "ORDER BY ts ASC";
    char query[512];
    snprintf(query, sizeof(query), sql, start_ts, end_ts);

    if (mysql_query(db_conn, query))
        return -1;
    MYSQL_RES *res = mysql_store_result(db_conn);
    if (!res)
        return -1;

    int n = mysql_num_rows(res);
    if (n == 0)
    {
        mysql_free_result(res);
        *count = 0;
        return 0;
    }

    double *t = malloc(n * sizeof(double));
    double *h = malloc(n * sizeof(double));
    char **desc = malloc(n * sizeof(char *));
    if (!t || !h || !desc)
    {
        free(t);
        free(h);
        free(desc);
        mysql_free_result(res);
        return -1;
    }

    MYSQL_ROW row;
    int i = 0;
    while ((row = mysql_fetch_row(res)) && i < n)
    {
        t[i] = atof(row[0]);
        h[i] = atof(row[1]);
        desc[i] = strdup(row[2] ? row[2] : "unknown");
        i++;
    }
    mysql_free_result(res);

    *temp_out = t;
    *humi_out = h;
    *desc_out = desc;
    *count = n;
    return 0;
}