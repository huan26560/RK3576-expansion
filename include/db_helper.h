#ifndef DB_HELPER_H
#define DB_HELPER_H

typedef struct {
    int id;
    char ts[20];
} db_record_info_t;

int db_init(void);
void db_save_dht11(float temp, float humi);
int db_get_record_count(void);
int db_get_recent_list(db_record_info_t *out, int max_count);
int db_export_csv(const char *filepath, char *msg, int msg_len);
int db_export_xlsx(const char *filepath, char *msg, int msg_len);   // 新增
void db_close(void);

#endif