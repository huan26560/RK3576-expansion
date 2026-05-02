#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mysql/mysql.h>
#include <xlsxwriter.h>
#include "db_helper.h"

static MYSQL *db_conn = NULL;

int db_init(void)
{
    if (db_conn != NULL) return 0;

    db_conn = mysql_init(NULL);
    if (db_conn == NULL) {
        fprintf(stderr, "[DB] mysql_init failed\n");
        return -1;
    }

    if (mysql_real_connect(db_conn, "127.0.0.1", "admin", "19981009huan",
                           "mydb", 3306, NULL, 0) == NULL) {
        fprintf(stderr, "[DB] Connect failed: %s\n", mysql_error(db_conn));
        mysql_close(db_conn);
        db_conn = NULL;
        return -1;
    }

    printf("[DB] Connected to MySQL\n");
    return 0;
}

void db_save_dht11(float temp, float humi)
{
    if (db_conn == NULL) {
        if (db_init() != 0) return;
    }

    char sql[256];
    snprintf(sql, sizeof(sql),
             "INSERT INTO sensor_data (temp, humi) VALUES (%.1f, %.1f)",
             temp, humi);

    if (mysql_query(db_conn, sql)) {
        fprintf(stderr, "[DB] INSERT failed: %s\n", mysql_error(db_conn));

        mysql_close(db_conn);
        db_conn = NULL;
        if (db_init() == 0) {
            if (mysql_query(db_conn, sql) == 0) {
                printf("[DB] Saved temp=%.1f, humi=%.1f (reconnect)\n", temp, humi);
            }
        }
    } else {
        printf("[DB] Saved temp=%.1f, humi=%.1f\n", temp, humi);
    }
}

void db_close(void)
{
    if (db_conn) {
        mysql_close(db_conn);
        db_conn = NULL;
        printf("[DB] Connection closed\n");
    }
}

/* ========== 获取表列表（供UI显示） ========== */

int db_get_table_list(db_table_info_t *out, int max_count)
{
    if (db_conn == NULL && db_init() != 0) return 0;

    /* 查询所有用户表（排除系统表） */
    if (mysql_query(db_conn,
        "SELECT table_name FROM information_schema.tables "
        "WHERE table_schema='mydb' AND table_type='BASE TABLE'")) {
        return 0;
    }

    MYSQL_RES *res = mysql_store_result(db_conn);
    if (!res) return 0;

    int i = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) && i < max_count) {
        const char *name = row[0];

        /* 跳过系统表 */
        if (strcmp(name, "tables_priv") == 0 ||
            strcmp(name, "columns_priv") == 0 ||
            strcmp(name, "db") == 0 ||
            strcmp(name, "user") == 0) {
            continue;
        }

        strncpy(out[i].name, name, sizeof(out[i].name) - 1);

        /* 显示名称：sensor_data → "Sensor Data" */
        if (strcmp(name, "sensor_data") == 0) {
            strcpy(out[i].desc, "Sensor Data");
        } else {
            strncpy(out[i].desc, name, sizeof(out[i].desc) - 1);
        }

        /* 查记录数 */
        char cnt_sql[128];
        snprintf(cnt_sql, sizeof(cnt_sql), "SELECT COUNT(*) FROM `%s`", name);
        if (mysql_query(db_conn, cnt_sql) == 0) {
            MYSQL_RES *cnt_res = mysql_store_result(db_conn);
            if (cnt_res) {
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
    if (db_conn == NULL && db_init() != 0) {
        snprintf(msg, msg_len, "DB Error");
        return -1;
    }

    /* 获取表的所有列名 */
    char cols_sql[256];
    snprintf(cols_sql, sizeof(cols_sql),
             "SELECT COLUMN_NAME FROM information_schema.COLUMNS "
             "WHERE TABLE_SCHEMA='mydb' AND TABLE_NAME='%s' ORDER BY ORDINAL_POSITION",
             tablename);

    if (mysql_query(db_conn, cols_sql)) {
        snprintf(msg, msg_len, "Get cols failed");
        return -1;
    }

    MYSQL_RES *cols_res = mysql_store_result(db_conn);
    if (!cols_res) {
        snprintf(msg, msg_len, "No columns");
        return -1;
    }

    char col_names[32][32];
    int col_count = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(cols_res)) && col_count < 32) {
        strncpy(col_names[col_count], row[0], 31);
        col_count++;
    }
    mysql_free_result(cols_res);

    if (col_count == 0) {
        snprintf(msg, msg_len, "Empty table");
        return -1;
    }

    /* 创建 xlsx */
    lxw_workbook  *workbook  = workbook_new(filepath);
    if (!workbook) {
        snprintf(msg, msg_len, "File Err");
        return -1;
    }

    lxw_worksheet *worksheet = workbook_add_worksheet(workbook, tablename);

    /* 写表头 */
    for (int c = 0; c < col_count; c++) {
        worksheet_write_string(worksheet, 0, c, col_names[c], NULL);
    }

    /* 查数据 */
    char data_sql[256];
    snprintf(data_sql, sizeof(data_sql), "SELECT * FROM `%s`", tablename);

    if (mysql_query(db_conn, data_sql)) {
        snprintf(msg, msg_len, "Query Err");
        workbook_close(workbook);
        return -1;
    }

    MYSQL_RES *res = mysql_store_result(db_conn);
    if (!res) {
        snprintf(msg, msg_len, "No Data");
        workbook_close(workbook);
        return -1;
    }

    int data_row = 1;
    MYSQL_ROW r;
    while ((r = mysql_fetch_row(res))) {
        for (int c = 0; c < col_count; c++) {
            /* 尝试数字，失败写字符串 */
            char *endptr;
            double val = strtod(r[c], &endptr);
            if (*endptr == '\0' && r[c][0] != '\0') {
                worksheet_write_number(worksheet, data_row, c, val, NULL);
            } else {
                worksheet_write_string(worksheet, data_row, c, r[c] ? r[c] : "", NULL);
            }
        }
        data_row++;
    }

    mysql_free_result(res);

    if (workbook_close(workbook) != 0) {
        snprintf(msg, msg_len, "Write Err");
        return -1;
    }

    snprintf(msg, msg_len, "OK %d rows", data_row - 1);
    return 0;
}
int db_get_preview(const char *tablename, int offset, db_preview_row_t *out, int max_rows)
{
    if (db_conn == NULL && db_init() != 0) return 0;

    char sql[256];
    snprintf(sql, sizeof(sql),
             "SELECT id, temp, humi, DATE_FORMAT(ts, '%%m-%%d %%H:%%i') "
             "FROM `%s` ORDER BY id DESC LIMIT %d OFFSET %d",
             tablename, max_rows, offset);

    if (mysql_query(db_conn, sql)) {
        fprintf(stderr, "[DB] Preview query failed: %s\n", mysql_error(db_conn));
        return 0;
    }

    MYSQL_RES *res = mysql_store_result(db_conn);
    if (!res) return 0;

    int i = 0;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) && i < max_rows) {
        out[i].id = atoi(row[0]);
        out[i].temp = atof(row[1]);
        out[i].humi = atof(row[2]);
        strncpy(out[i].ts, row[3], sizeof(out[i].ts) - 1);
        i++;
    }
    mysql_free_result(res);
    return i;
}