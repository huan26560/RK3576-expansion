/*
 * menu.c - 图标式主菜单（全屏单图标，28x28像素）
 * 优化版本：简化动画逻辑，删除无效代码
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
#include "page_screensaver.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define ICON_SIZE 28
#define ICON_START_Y 18

static menu_item_t *menu_root = NULL;
menu_item_t *menu_current = NULL;
static int menu_cursor = 0;
int menu_list_active = 0;
static int dashboard_last_cursor = 0;

void menu_init(void)
{
    menu_root = page_dashboard_create_menu();

    menu_add_child(menu_root, page_docker_create_menu());
    menu_add_child(menu_root, page_system_create_menu());
    menu_add_child(menu_root, page_env_create_menu());
    menu_add_child(menu_root, page_network_create_menu());
    menu_add_child(menu_root, page_tools_create_menu());

    menu_current = menu_root;
    menu_cursor = 0;
    dashboard_last_cursor = 0;

    screensaver_init();
}

void menu_enter_list(void)
{
    menu_list_active = 1;
    menu_cursor = dashboard_last_cursor;
    screensaver_reset_idle();
}

void menu_exit_list(void)
{
    menu_list_active = 0;
    screensaver_reset_idle();
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
    // 1. 优先让屏保处理事件
    if (screensaver_handle_event(ev))
    {
        return;
    }

    // 2. 非屏保状态：重置屏保计时
    screensaver_reset_idle();

    // 3. 原有菜单事件逻辑
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
            if (ev == EV_DOWN)
            {
                menu_cursor = (menu_cursor + 1) % menu_root->child_count;
            }
            else if (ev == EV_UP)
            {
                menu_cursor = (menu_cursor - 1 + menu_root->child_count) % menu_root->child_count;
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

// 优化后的draw_single_icon：移除动画逻辑，直接绘制
static void draw_single_icon(void)
{
    if (menu_cursor >= menu_root->child_count || !menu_root->children[menu_cursor]->icon)
        return;

    menu_item_t *item = menu_root->children[menu_cursor];

    // 计算居中位置
    int base_x = (OLED_WIDTH - ICON_SIZE) / 2;
    int base_y = ICON_START_Y;

    // 绘制图标
    hal_oled_draw_icon(base_x, base_y, ICON_SIZE, ICON_SIZE, item->icon);
    
    // 文字标签（可选，注释掉可隐藏）
    int text_x = (OLED_WIDTH - strlen(item->name) * 6) / 2;
    hal_oled_string(text_x, base_y + ICON_SIZE + 5, item->name);
}

void menu_render(void)
{
    // 1. 调用screensaver_draw更新屏保状态（必要步骤）
    screensaver_draw();
    
    // 2. 如果屏保激活，直接返回（已绘制屏保内容）
    if (screensaver_is_active())
    {
        hal_oled_refresh();
        return;
    }

    // 3. 非屏保状态：绘制菜单
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

            // 绘制单个图标
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
    screensaver_reset_idle();
    if (menu_current->child_count > 0)
    {
        dashboard_last_cursor = 0;
        menu_current = menu_current->children[0];
        menu_cursor = 0;
    }
}

void menu_enter(void)
{
    screensaver_reset_idle();
    menu_list_active = 1;
    menu_cursor = dashboard_last_cursor;
}

void menu_back(void)
{
    screensaver_reset_idle();
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