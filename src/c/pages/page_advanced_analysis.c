#include "menu.h"
#include "hal_oled.h"
#include "cpp/advanced_analysis.h"
#include <stdio.h>
#include <string.h>
#include <font.h>
#include <stdlib.h>

static int analysis_ready = 0;
static int analysis_page = 0;
static int total_pages = 0;
static char report_buffer[2048] = {0};
static char display_lines[5][24]; // 改为5行，充分利用屏幕

// 从报告提取所有行
static char *all_lines[64];
static int line_count = 0;

static void parse_report_lines(const char *report)
{
    line_count = 0;
    memset(display_lines, 0, sizeof(display_lines));

    char temp[2048];
    strncpy(temp, report, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';

    char *line = strtok(temp, "\n");
    while (line && line_count < 64)
    {
        // 跳过空行和分隔线
        if (strlen(line) > 0 && !strstr(line, "===") && !strstr(line, "═══"))
        {
            all_lines[line_count] = strdup(line);
            line_count++;
        }
        line = strtok(NULL, "\n");
    }

    // 计算总页数（每页5行）
    total_pages = (line_count + 4) / 5;
    if (total_pages == 0)
        total_pages = 1;

    if (analysis_page >= total_pages)
    {
        analysis_page = 0;
    }
}

static void update_display_lines(void)
{
    memset(display_lines, 0, sizeof(display_lines));

    int start = analysis_page * 5;
    for (int i = 0; i < 5 && (start + i) < line_count; i++)
    {
        strncpy(display_lines[i], all_lines[start + i], 23);
        display_lines[i][23] = '\0';
    }
}

static void perform_analysis(void)
{
    memset(report_buffer, 0, sizeof(report_buffer));
    memset(display_lines, 0, sizeof(display_lines));

    for (int i = 0; i < line_count; i++)
    {
        if (all_lines[i])
        {
            free(all_lines[i]);
            all_lines[i] = NULL;
        }
    }
    line_count = 0;

    if (perform_advanced_analysis(report_buffer, sizeof(report_buffer)) == 0)
    {
        parse_report_lines(report_buffer);
        update_display_lines();
        analysis_ready = 1;
    }
    else
    {
        strcpy(display_lines[0], "No data available");
        analysis_ready = -1;
    }
}

static void page_advanced_analysis_draw(void)
{
    if (!analysis_ready)
    {
        hal_oled_clear();
        hal_oled_string(0, 0, "Advanced Analysis");
        hal_oled_line(0, 10, 127, 10);
        hal_oled_string(10, 30, "Analyzing...");
        hal_oled_refresh();
        perform_analysis();
    }

    hal_oled_clear();
    hal_oled_string(0, 0, "Advanced Analysis");
    hal_oled_line(0, 10, 127, 10);

    // 页码指示
    char page_indicator[16];
    snprintf(page_indicator, sizeof(page_indicator), "%d/%d", analysis_page + 1, total_pages);
    int x = 128 - strlen(page_indicator) * 6;
    hal_oled_string(x, 0, page_indicator);

    // 上下箭头指示（只在有更多内容时显示）
    if (analysis_page > 0)
    {
        hal_oled_string(120, 12, "^");
    }
    if (analysis_page < total_pages - 1)
    {
        hal_oled_string(120, 50, "v");
    }

    // 显示5行内容
    int y = 14;
    for (int i = 0; i < 5; i++)
    {
        if (display_lines[i][0])
        {
            hal_oled_string(0, y, display_lines[i]);
            y += 10;
        }
    }

    hal_oled_refresh();
}

static void page_advanced_analysis_handle_event(event_t ev)
{
    switch (ev)
    {
    case EV_UP:
        if (analysis_page > 0)
        {
            analysis_page--;
            update_display_lines();
        }
        break;
    case EV_DOWN:
        if (analysis_page < total_pages - 1)
        {
            analysis_page++;
            update_display_lines();
        }
        break;
    case EV_BACK:
        for (int i = 0; i < line_count; i++)
        {
            if (all_lines[i])
            {
                free(all_lines[i]);
                all_lines[i] = NULL;
            }
        }
        line_count = 0;
        analysis_ready = 0;
        analysis_page = 0;
        menu_back();
        break;
    default:
        break;
    }
}

void page_advanced_analysis_init(void)
{
    static int init = 0;
    if (init)
        return;
    page_register("Analysis", page_advanced_analysis_draw, page_advanced_analysis_handle_event);
    init = 1;
}

menu_item_t *page_advanced_analysis_create_menu(void)
{
    page_advanced_analysis_init();
    extern const unsigned char icon_data_analyzer_28x28[];
    return menu_create("Analysis", page_advanced_analysis_draw, icon_data_analyzer_28x28);
}