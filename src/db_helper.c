#include <stdio.h>
#include <string.h>
#include <mysql/mysql.h>
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

/* 新版：一行存 temp + humi */
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

        /* 重连一次 */
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
#include <xlsxwriter.h>

int db_export_xlsx(const char *filepath, char *msg, int msg_len)
{
    if (db_conn == NULL && db_init() != 0) {
        snprintf(msg, msg_len, "DB Error");
        return -1;
    }

    lxw_workbook  *workbook  = workbook_new(filepath);
    if (!workbook) {
        snprintf(msg, msg_len, "File Err");
        return -1;
    }

    lxw_worksheet *worksheet = workbook_add_worksheet(workbook, "Sensor");

    /* 表头 */
    worksheet_write_string(worksheet, 0, 0, "ID", NULL);
    worksheet_write_string(worksheet, 0, 1, "Temp", NULL);
    worksheet_write_string(worksheet, 0, 2, "Humi", NULL);
    worksheet_write_string(worksheet, 0, 3, "Time", NULL);

    if (mysql_query(db_conn,
        "SELECT id,temp,humi,DATE_FORMAT(ts,'%Y-%m-%d %H:%i:%s') "
        "FROM sensor_data ORDER BY id")) {
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

    int row = 1;
    MYSQL_ROW r;
    while ((r = mysql_fetch_row(res))) {
        worksheet_write_number(worksheet, row, 0, atoi(r[0]), NULL);
        worksheet_write_number(worksheet, row, 1, atof(r[1]), NULL);
        worksheet_write_number(worksheet, row, 2, atof(r[2]), NULL);
        worksheet_write_string(worksheet, row, 3, r[3], NULL);
        row++;
    }

    mysql_free_result(res);

    if (workbook_close(workbook) != 0) {
        snprintf(msg, msg_len, "Write Err");
        return -1;
    }

    snprintf(msg, msg_len, "OK %d rows", row - 1);
    return 0;
}