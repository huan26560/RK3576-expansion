#ifndef DB_HELPER_H
#define DB_HELPER_H

typedef struct {
    char name[32];
    char desc[32];
    int count;
} db_table_info_t;

typedef struct {
    int id;
    float temp;
    float humi;
    char ts[16];   // MM-DD HH:MM
} db_preview_row_t;

int db_init(void);
void db_save_dht11(float temp, float humi);
int db_get_table_list(db_table_info_t *out, int max_count);
int db_get_preview(const char *tablename, int offset, db_preview_row_t *out, int max_rows);
int db_export_xlsx(const char *tablename, const char *filepath, char *msg, int msg_len);
void db_close(void);

#endif