#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "hal_oled.h"
#include "page_interface.h"

#define MAX_DISKS 10
#define LINE_H 18
#define BAR_WIDTH 65
#define LABEL_WIDTH 40
#define MAX_DISPLAY 3

typedef struct {
    char device[16];
    char mount[32];
    long total_mb;
    long used_mb;
    int use_percent;
} disk_info_t;

static disk_info_t disks[MAX_DISKS];
static int disk_count = 0;
static int selected_disk = 0;

// 命令执行
static int exec_cmd(const char *cmd, char *output, int len)
{
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    
    int n = fread(output, 1, len - 1, fp);
    output[n] = '\0';
    
    int ret = pclose(fp);
    if (ret == -1) {
        printf("[Disk] Command failed: %s\n", cmd);
        return -1;
    }
    
    printf("[Disk] Execute: %s\n", cmd);
    printf("[Disk] Result: %s\n", output);
    return 0;
}

static void parse_disk_info(void)
{
    disk_count = 0;
    char result[2048];
    
    if (exec_cmd("df -BM -x tmpfs -x devtmpfs --output=source,size,used,pcent 2>&1", result, sizeof(result)) < 0) {
        printf("[Disk] Failed to get disk info\n");
        return;
    }
    
    char *line = strtok(result, "\n");
    int line_num = 0;
    
    while (line && disk_count < MAX_DISKS) {
        if (++line_num == 1) {
            line = strtok(NULL, "\n");
            continue;
        }
        
        disk_info_t *d = &disks[disk_count];
        char size_str[16], used_str[16], percent_str[8];
        
        if (sscanf(line, "%s %s %s %s", d->device, size_str, used_str, percent_str) == 4) {
            d->total_mb = atol(size_str);
            d->used_mb = atol(used_str);
            d->use_percent = atoi(percent_str);
            
            printf("[Disk] Parsed: %s, %ldMB, %d%%\n", d->device, d->total_mb, d->use_percent);
            disk_count++;
        } else {
            printf("[Disk] Parse failed: %s\n", line);
        }
        
        line = strtok(NULL, "\n");
    }
    
    printf("[Disk] Total disks: %d\n", disk_count);
}

void page_disk_draw(void)
{
    printf("[Disk] Draw called, disk_count=%d, selected_disk=%d\n", disk_count, selected_disk);
    
    if (disk_count == 0) {
        parse_disk_info();
    }
    
    if (disk_count == 0) {
        hal_oled_clear();
        hal_oled_string(0, 25, "No disks");
        return;
    }
    
    int scroll_offset = 0;
    if (disk_count > MAX_DISPLAY) {
        scroll_offset = (selected_disk >= MAX_DISPLAY) ? selected_disk - (MAX_DISPLAY - 1) : 0;
    }
    
    printf("[Disk] scroll_offset=%d, showing disks %d to %d\n", 
           scroll_offset, scroll_offset, scroll_offset + MAX_DISPLAY - 1);
    
    hal_oled_clear();
    hal_oled_string(0, 0, "Disk Manager");
    hal_oled_line(0, 9, 127, 9);
    
    int y = 18;
    for (int i = scroll_offset; i < disk_count && i < scroll_offset + MAX_DISPLAY; i++) {
        disk_info_t *d = &disks[i];
        
        char label[20];
        snprintf(label, sizeof(label), "%s %s", i == selected_disk ? ">" : " ", d->device);
        hal_oled_string(0, y, label);
        
        int bar_y = y + 8;
        hal_oled_draw_progress_bar(LABEL_WIDTH, bar_y, BAR_WIDTH, d->use_percent, "");
        
        char percent[6];
        snprintf(percent, sizeof(percent), "%d%%", d->use_percent);
        hal_oled_string(LABEL_WIDTH + BAR_WIDTH + 3, y, percent);
        
        y += LINE_H;
    }
    
    hal_oled_string(0, 55, "Key2=Info Hold=Back");
}

void page_disk_handle_event(event_t ev)
{
    printf("[Disk] Event: %d (UP=%d, DOWN=%d, ENTER=%d, BACK=%d)\n", 
           ev, EV_UP, EV_DOWN, EV_ENTER, EV_BACK);
    
    switch (ev) {
        case EV_UP:
            printf("[Disk] EV_UP, selected_disk=%d\n", selected_disk);
            if (selected_disk > 0) {
                selected_disk--;
                printf("[Disk] New selected_disk=%d\n", selected_disk);
            }
            break;
            
        case EV_DOWN:
            printf("[Disk] EV_DOWN, selected_disk=%d\n", selected_disk);
            if (selected_disk < disk_count - 1) {
                selected_disk++;
                printf("[Disk] New selected_disk=%d\n", selected_disk);
            }
            break;
            
        case EV_ENTER:
            printf("[Disk] EV_ENTER\n");
            hal_oled_clear();
            hal_oled_string(0, 0, "Disk Detail");
            hal_oled_line(0, 9, 127, 9);
            
            if (selected_disk < disk_count) {
                disk_info_t *d = &disks[selected_disk];
                
                char buf[64];
                snprintf(buf, sizeof(buf), "Dev: %s", d->device);
                hal_oled_string(0, 18, buf);
                
                snprintf(buf, sizeof(buf), "Size: %ldMB", d->total_mb);
                hal_oled_string(0, 28, buf);
                
                snprintf(buf, sizeof(buf), "Used: %ldMB", d->used_mb);
                hal_oled_string(80, 28, buf);
                
                snprintf(buf, sizeof(buf), "Usage: %d%%", d->use_percent);
                hal_oled_string(0, 38, buf);
                
                hal_oled_string(0, 55, "Hold Key2=Back");
                printf("[Disk] Showed detail for disk %d\n", selected_disk);
            }
            break;
            
        case EV_BACK:
            printf("[Disk] EV_BACK - exiting\n");
            extern void menu_back(void);
            menu_back();
            break;
            
        default:
            printf("[Disk] Unknown event: %d\n", ev);
            break;
    }
}

page_interface_t disk_interface = {
    .name = "Disk",
    .draw = page_disk_draw,
    .handle_event = page_disk_handle_event,
    .on_enter = NULL,
    .on_exit = NULL
};