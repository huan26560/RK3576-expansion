/*
 * menu.c - 图标式主菜单（全屏单图标，28x28像素）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "menu.h"
#include "hal_oled.h"
#include "hal_dht11.h"
#include "page_interface.h"
#include "hal_gpio.h"
#include "font.h"
#include "popup.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define ICON_SIZE 28
#define ICON_START_Y 18

#define ANIM_FRAMES 0
#define ANIM_SPEED 2

static menu_item_t *menu_root = NULL;
menu_item_t *menu_current = NULL;
static int menu_cursor = 0;
int menu_list_active = 0;

typedef enum
{
    ANIM_IDLE,
    ANIM_SLIDING_LEFT,
    ANIM_SLIDING_RIGHT
} anim_state_t;

static anim_state_t anim_state = ANIM_IDLE;
static int anim_frame = 0;
static int dashboard_last_cursor = 0;
void menu_enter_list(void)
{
    menu_list_active = 1;
    menu_cursor = dashboard_last_cursor;
    anim_state = ANIM_IDLE;
    printf("进入图标菜单\n");
}

void menu_exit_list(void)
{
    menu_list_active = 0;
    printf("返回 Dashboard\n");
}


void menu_init(void)
{
    menu_root = page_dashboard_create_menu();

    menu_add_child(menu_root, page_docker_create_menu());
    menu_add_child(menu_root, page_system_create_menu());
    menu_add_child(menu_root, page_env_create_menu());
    menu_add_child(menu_root, page_network_create_menu());
    menu_add_child(menu_root, page_tools_create_menu());
    // menu_add_child(menu_root, page_terminal_create_menu());
    menu_add_child(menu_root, menu_create(" Home", page_home_draw, icon_home_28x28));
    menu_current = menu_root;
    menu_cursor = 0;
    dashboard_last_cursor = 0;
}

menu_item_t *menu_create(const char *name, page_draw_t draw_func, const unsigned char *icon)
{
    menu_item_t *item = calloc(1, sizeof(menu_item_t));
    item->name = strdup(name);
    item->draw_func = draw_func;
    item->icon = icon;
    item->parent = NULL;
    item->children = NULL;
    item->child_count = 0;
    return item;
}

void menu_add_child(menu_item_t *parent, menu_item_t *child)
{
    parent->children = realloc(parent->children, (parent->child_count + 1) * sizeof(menu_item_t *));
    parent->children[parent->child_count] = child;
    child->parent = parent;
    parent->child_count++;
}

void menu_handle_event(event_t ev)
{
    if (popup_handle_event(ev))
    {
        return;
    }
    // ===== 三按键逻辑与您的原始文件完全一致 =====
    if (strcmp(menu_current->name, "Dashboard") == 0)
    {
        if (ev == EV_ENTER && !menu_list_active)
        {
            menu_enter_list();
            return;
        }
        else if (ev == EV_BACK && menu_list_active)
        {
            menu_exit_list();
            return;
        }
        else if (menu_list_active)
        {
            // **修复原始bug**：正确区分UP/DOWN
            if (ev == EV_DOWN)
            {
                menu_cursor = (menu_cursor + 1) % menu_root->child_count;
                anim_state = ANIM_SLIDING_LEFT;
                anim_frame = 0;
            }
            else if (ev == EV_UP)
            {
                menu_cursor = (menu_cursor - 1 + menu_root->child_count) % menu_root->child_count;
                anim_state = ANIM_SLIDING_RIGHT;
                anim_frame = 0;
            }

            if (ev == EV_ENTER && menu_cursor < menu_root->child_count)
            {
                dashboard_last_cursor = menu_cursor;
                menu_current = menu_root->children[menu_cursor];
                menu_cursor = 0;
                menu_list_active = 0;
            }
            return;
        }
    }

    page_interface_t *iface = page_get_interface(menu_current->name);
    if (iface && iface->handle_event)
    {
        iface->handle_event(ev);
        return;
    }

    switch (ev)
    {
    case EV_DOWN:
        if (menu_current->child_count > 0)
        {
            menu_cursor = (menu_cursor + 1) % menu_current->child_count;
        }
        break;

    case EV_UP:
        if (menu_current->child_count > 0)
        {
            menu_cursor = (menu_cursor - 1 + menu_current->child_count) % menu_current->child_count;
        }
        break;

    case EV_ENTER:
        if (menu_current->child_count > 0 && menu_cursor < menu_current->child_count)
        {
            menu_current = menu_current->children[menu_cursor];
            menu_cursor = 0;
        }
        break;

    case EV_BACK:
        if (menu_current->parent)
        {
            menu_current = menu_current->parent;
            menu_cursor = 0;
            if (strcmp(menu_current->name, "Dashboard") == 0)
            {
                menu_list_active = 1;
            }
        }
        break;
    }
}

// **核心修改：只绘制当前选中的单个图标**
static void draw_single_icon(void)
{
    if (menu_cursor >= menu_root->child_count || !menu_root->children[menu_cursor]->icon)
        return;

    menu_item_t *item = menu_root->children[menu_cursor];

    // 计算居中位置
    int base_x = (OLED_WIDTH - ICON_SIZE) / 2;
    int base_y = ICON_START_Y;

    // 滑入动画偏移
    int offset_x = 0;
    if (anim_state == ANIM_SLIDING_LEFT && anim_frame < ANIM_FRAMES)
    {
        offset_x = (ANIM_FRAMES - anim_frame) * ANIM_SPEED;
        anim_frame++;
    }
    else if (anim_state == ANIM_SLIDING_RIGHT && anim_frame < ANIM_FRAMES)
    {
        offset_x = -(ANIM_FRAMES - anim_frame) * ANIM_SPEED;
        anim_frame++;
    }
    else
    {
        anim_state = ANIM_IDLE;
    }

    int x = base_x + offset_x;
    int y = base_y;

    // 绘制图标
    hal_oled_draw_icon(x, y, ICON_SIZE, ICON_SIZE, item->icon);

    // **文字标签（可选，注释掉可隐藏）**
    int text_x = (OLED_WIDTH - strlen(item->name) * 6) / 2 + offset_x;
    hal_oled_string(text_x, y + ICON_SIZE + 5, item->name);

    // 选中框装饰
    // hal_oled_rect(x - 2, y - 2, ICON_SIZE + 4, ICON_SIZE + 4, 1);
    // hal_oled_rect(x - 1, y - 1, ICON_SIZE + 2, ICON_SIZE + 2, 1);
}

void menu_render(void)
{
    if (!menu_current)
        return;

    hal_oled_clear();

    if (strcmp(menu_current->name, "Dashboard") == 0)
    {
        if (!menu_list_active)
        {
            if (menu_current->draw_func)
            {
                menu_current->draw_func();
            }
        }
        else
        {
            // 顶部标题栏
            hal_oled_string(38, 0, "Main Menu");
            hal_oled_line(0, 9, 127, 9);

            // **绘制单个图标**
            draw_single_icon();
        }
    }
    else
    {
        // 子菜单保持原样
        hal_oled_string(0, 0, menu_current->name);
        hal_oled_line(0, 9, 127, 9);

        page_interface_t *iface = page_get_interface(menu_current->name);
        if (iface && iface->draw)
        {
            iface->draw();
        }
        else if (menu_current->draw_func)
        {
            menu_current->draw_func();
        }
    }
    popup_draw();
    hal_oled_refresh();
}

menu_item_t *menu_get_current(void)
{
    return menu_current;
}

int menu_get_cursor(void)
{
    return menu_cursor;
}

int menu_is_dashboard(void)
{
    return strcmp(menu_current->name, "Dashboard") == 0;
}

void menu_enter_first_child(void)
{
    if (menu_current->child_count > 0)
    {
        dashboard_last_cursor = 0;
        menu_current = menu_current->children[0];
        menu_cursor = 0;
    }
}

void menu_enter(void)
{
    menu_list_active = 1;
    menu_cursor = dashboard_last_cursor;
}

void menu_back(void)
{
    if (menu_current->parent)
    {
        menu_current = menu_current->parent;
        menu_cursor = 0;
    }
    if (strcmp(menu_current->name, "Dashboard") == 0)
    {
        menu_cursor = dashboard_last_cursor;
        menu_list_active = 1;
    }
}

void page_disk_draw(void) {}
void page_home_draw(void) {}