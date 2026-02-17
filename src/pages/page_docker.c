#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "hal_oled.h"
#include "hal_system.h"
#include "menu.h"
#include "page_interface.h"
#include "thread.h"
#include "font.h"
#include <stdint.h>

#define MAX_CONTAINERS 20
#define LINE_HEIGHT 9
#define MAX_DISPLAY 5
#define OLED_WIDTH 128
#define CHAR_WIDTH 6

// 容器数据结构
typedef struct
{
    char id[33];     // 32 + 1
    char name[65];   // 64 + 1
    char status[33]; // 32 + 1
} container_t;

// 模块静态数据
static container_t containers[MAX_CONTAINERS];
static int container_count = 0;
static int selected_container = 0;
static int scroll_offset = 0;
static uint64_t last_scroll_time = 0;
static int name_scroll_pos[MAX_CONTAINERS] = {0};

static container_t *current_container = NULL;

// Docker 菜单树
static menu_item_t *docker_root = NULL;
static menu_item_t *container_list_node = NULL;

static int docker_state = 0;            // 0=列表页, 1=详情页, 2=确认页
static int need_refresh_containers = 1; // 解决闪烁的核心标志：仅需要时刷新列表

extern menu_item_t *menu_current; // 在 menu.c 中定义

//===========函数声明============//
void page_container_detail_draw(void);
static void page_container_list_draw(void);
void page_restart_confirm_draw(void);
static void refresh_container_list(void);

// ========== 工具函数（恢复原有解析逻辑，仅保留必要过滤）==========

static int exec_docker_cmd(const char *cmd, char *output, int len)
{
    FILE *fp = popen(cmd, "r");
    if (!fp)
        return -1;
    int n = fread(output, 1, len - 1, fp);
    output[n] = 0;
    pclose(fp);
    return 0;
}

static void parse_containers(void)
{
    container_count = 0;
    char result[4096] = {0};

    // 执行 docker 命令，若失败则直接返回
    if (exec_docker_cmd("docker ps -a --no-trunc --format 'ID={{.ID}}|NAME={{.Names}}|STATUS={{.Status}}' 2>&1", result, sizeof(result)) != 0)
    {
        return;
    }

    char *line = strtok(result, "\n");
    while (line && container_count < MAX_CONTAINERS)
    {
        // 跳过空行
        if (strlen(line) == 0)
        {
            line = strtok(NULL, "\n");
            continue;
        }

        char *id_start = strstr(line, "ID=");
        char *name_start = strstr(line, "NAME=");
        char *status_start = strstr(line, "STATUS=");

        if (id_start && name_start && status_start)
        {
            char *id_end = strchr(id_start + 3, '|');
            char *name_end = strchr(name_start + 5, '|');

            if (id_end && name_end)
            {
                container_t temp;
                memset(&temp, 0, sizeof(temp));

                // 提取 ID
                snprintf(temp.id, sizeof(temp.id), "%.*s",
                         (int)(id_end - (id_start + 3)), id_start + 3);
                // 提取 Name
                snprintf(temp.name, sizeof(temp.name), "%.*s",
                         (int)(name_end - (name_start + 5)), name_start + 5);
                // 提取 Status（直接到行尾）
                snprintf(temp.status, sizeof(temp.status), "%s", status_start + 7);

                // 去重：检查是否已存在相同 ID 的容器
                int found = 0;
                for (int i = 0; i < container_count; i++)
                {
                    if (strcmp(containers[i].id, temp.id) == 0)
                    {
                        // 已存在，则更新其名称和状态（可能容器状态发生变化）
                        strcpy(containers[i].name, temp.name);
                        strcpy(containers[i].status, temp.status);
                        found = 1;
                        break;
                    }
                }

                // 不存在相同 ID，则作为新容器添加
                if (!found)
                {
                    containers[container_count++] = temp;
                }
            }
        }
        line = strtok(NULL, "\n");
    }
}

// 保留解决闪烁的核心：仅需要时刷新列表（不重复解析）
static void refresh_container_list(void)
{
    if (need_refresh_containers)
    {
        parse_containers();
        need_refresh_containers = 0;

        // 仅做基础的选中项越界保护（不修改偏移逻辑）
        if (selected_container >= container_count && container_count > 0)
        {
            selected_container = container_count - 1;
        }
        else if (container_count == 0)
        {
            selected_container = 0;
        }
    }
}

// 恢复你原有版本的滚动文字绘制逻辑（无重复显示问题）
static int text_width(const char *str)
{
    return strlen(str) * CHAR_WIDTH;
}

static void render_with_scroll(int x, int y, const char *text, int max_width, int scroll_pos)
{
    int width = text_width(text);
    if (width <= max_width)
    {
        hal_oled_string(x, y, text);
        return;
    }

    int offset = scroll_pos % (width + 20);
    int start_x = x - offset;

    char temp[64];
    strncpy(temp, text, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = 0;

    // 恢复原有清理逻辑
    int max_chars = (OLED_WIDTH - start_x) / CHAR_WIDTH + 1;
    if (max_chars > 0 && max_chars < (int)strlen(temp))
    {
        temp[max_chars] = 0;
    }

    hal_oled_string(start_x, y, temp);

    if (start_x + width < x + max_width)
    {
        int chars_fit = (OLED_WIDTH - (start_x + width + 20)) / CHAR_WIDTH;
        if (chars_fit > 0)
        {
            temp[chars_fit] = 0;
            hal_oled_string(start_x + width + 20, y, temp);
        }
    }
}

static void build_detail_lines(container_t *c, char lines[][71])
{
    snprintf(lines[0], 71, "Name: %s", c->name);
    snprintf(lines[1], 71, "ID: %.12s", c->id);
    snprintf(lines[2], 71, "Status: %s", c->status);
}

// ========== 页面绘制函数（保留无闪烁+单次确认的核心）==========

static void page_docker_root_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Docker Manager");
    hal_oled_line(0, 10, 127, 10); // 添加标题下方横线

    refresh_container_list(); // 仅需要时解析，避免重复

    char buf[64];
    snprintf(buf, sizeof(buf), "Containers: %d", container_count);
    hal_oled_string(0, 25, buf);

    int active_count = 0;
    for (int i = 0; i < container_count; i++)
    {
        if (strstr(containers[i].status, "Up"))
        {
            active_count++;
        }
    }
    snprintf(buf, sizeof(buf), "Active: %d", active_count);
    hal_oled_string(0, 35, buf);

    hal_oled_string(0, 55, "Press ENTER to view");
    hal_oled_refresh();
}

// 保留原有列表绘制逻辑（仅替换parse为refresh_container_list）
static void page_container_list_draw(void)
{
    if (docker_state == 1)
    {
        page_container_detail_draw();
        return;
    }
    else if (docker_state == 2)
    {
        page_restart_confirm_draw();
        return;
    }

    hal_oled_clear();
    hal_oled_string(0, 0, "Containers");
    hal_oled_line(0, 10, 127, 10); // 添加标题下方横线

    refresh_container_list(); // 核心：仅需要时解析，解决闪烁

    if (container_count == 0)
    {
        hal_oled_string(0, 25, "No containers");
        hal_oled_string(0, 40, "Press ENTER to refresh");
    }
    else
    {
        // 恢复你原有滚动逻辑
        if (selected_container < scroll_offset)
        {
            scroll_offset = selected_container;
        }
        else if (selected_container >= scroll_offset + MAX_DISPLAY)
        {
            scroll_offset = selected_container - MAX_DISPLAY + 1;
        }

        uint64_t now = millis();
        if (now - last_scroll_time > 500)
        {
            for (int i = 0; i < container_count; i++)
            {
                name_scroll_pos[i] += 2;
            }
            last_scroll_time = now;
        }

        int y = 18;
        for (int i = scroll_offset; i < container_count && i < scroll_offset + MAX_DISPLAY; i++)
        {
            char line[67];
            char *running = strstr(containers[i].status, "Up") ? "UP" : "STOP";
            snprintf(line, sizeof(line), "%s %s|%s",
                     i == selected_container ? ">" : " ",
                     containers[i].name, running);

            char *sep = strchr(line, '|');
            if (sep)
            {
                *sep = 0;
                char *prefix = line;
                char *name = line + 2;
                char *status = sep + 1;

                hal_oled_string(0, y, prefix);
                render_with_scroll(10, y, name, OLED_WIDTH - 10 - 30, name_scroll_pos[i]);
                hal_oled_string(OLED_WIDTH - 30, y, status);
            }
            else
            {
                hal_oled_string(0, y, line);
            }
            y += 9;
        }
    }
    hal_oled_refresh();
}

void page_container_detail_draw(void)
{
    if (!current_container)
    {
        docker_state = 0;
        return; // 移除重复draw调用，解决闪烁
    }

    hal_oled_clear();
    hal_oled_string(0, 0, "Container Detail");
    hal_oled_line(0, 10, 127, 10);

    char lines[3][71];
    build_detail_lines(current_container, lines);

    int y = 18;
    for (int i = 0; i < 3; i++)
    {
        hal_oled_string(0, y, lines[i]);
        y += 12;
    }
    hal_oled_refresh();
}

void page_restart_confirm_draw(void)
{
    hal_oled_clear();
    hal_oled_string(0, 0, "Confirm");
    hal_oled_line(0, 10, 127, 10);

    hal_oled_string(10, 30, "Restart container?");
    hal_oled_refresh();
}

// ========== 事件处理（保留单次确认+无闪烁核心）==========

void page_docker_handle_event(event_t ev)
{
    const char *current_name = menu_current->name;

    if (strcmp(current_name, "Docker") == 0)
    {
        switch (ev)
        {
        case EV_ENTER:
            menu_current = docker_root->children[0];
            selected_container = 0;
            scroll_offset = 0;
            docker_state = 0;
            need_refresh_containers = 1; // 进入列表页强制刷新
            page_container_list_draw();
            break;
        case EV_BACK:
            extern void menu_back(void);
            menu_back();
            break;
        }
        return;
    }

    if (strcmp(current_name, "Containers") == 0)
    {
        if (docker_state == 1)
        {
            switch (ev)
            {
            case EV_ENTER:
                docker_state = 2;
                page_restart_confirm_draw();
                break;
            case EV_BACK:
                docker_state = 0;
                current_container = NULL;
                // 移除重复draw调用，解决返回闪烁
                break;
            }
            return;
        }

        if (docker_state == 2)
        {
            switch (ev)
            {
            case EV_ENTER:
                if (current_container)
                {
                    char cmd[128];
                    snprintf(cmd, sizeof(cmd), "docker restart %s", current_container->id);
                    system(cmd);
                    need_refresh_containers = 1; // 重启后强制刷新
                }
                docker_state = 0;
                current_container = NULL;
                break;
            case EV_BACK:
                docker_state = 1;
                page_container_detail_draw();
                break;
            }
            return;
        }

        switch (ev)
        {
        case EV_UP:
            selected_container--;
            if (selected_container < 0)
                selected_container = container_count - 1;
            page_container_list_draw();
            break;
        case EV_DOWN:
            selected_container++;
            if (selected_container >= container_count)
                selected_container = 0;
            page_container_list_draw();
            break;
        case EV_ENTER:
            if (container_count > 0)
            {
                current_container = &containers[selected_container];
                docker_state = 1;
                page_container_detail_draw(); // 单次确认进入详情页
            }
            break;
        case EV_BACK:
            extern void menu_back(void);
            menu_back();
            break;
        }
        return;
    }
}

// ========== 初始化 & 菜单创建（无修改）==========

void page_docker_init(void)
{
    static int initialized = 0;
    if (initialized)
        return;

    page_register("Docker", page_docker_root_draw, page_docker_handle_event);
    page_register("Containers", page_container_list_draw, page_docker_handle_event);

    initialized = 1;
}

menu_item_t *page_docker_create_menu(void)
{
    page_docker_init();

    if (docker_root == NULL)
    {
        docker_root = menu_create("Docker", page_docker_root_draw, icon_docker_28x28);
        container_list_node = menu_create("Containers", page_container_list_draw, NULL);
        menu_add_child(docker_root, container_list_node);
    }
    return docker_root;
}