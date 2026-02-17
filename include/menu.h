#ifndef MENU_H
#define MENU_H

#include <stdint.h>
#include <page_interface.h>


typedef void (*page_draw_t)(void);

typedef struct menu_item {
    char *name;
    page_draw_t draw_func;
    const unsigned char *icon;  // 32x32图标数据
    struct menu_item *parent;
    struct menu_item **children;
    int child_count;
} menu_item_t;

extern int menu_in_view;  // 0=Dashboard, 1=菜单列表
// 函数声明
// menu.h 顶部添加
// 新增：菜单列表状态
extern int menu_list_active;  // 0=Dashboard, 1=显示主菜单列表

void menu_init(void);
menu_item_t *menu_create(const char *name, page_draw_t draw_func, const unsigned char *icon);

void menu_add_child(menu_item_t *parent, menu_item_t *child);
void menu_handle_event(event_t ev);
void menu_render(void);
menu_item_t *menu_get_current(void);
int menu_get_cursor(void);

// 新增函数
void menu_enter_list(void);   // 进入主菜单列表
void menu_exit_list(void);    // 退出主菜单列表返回 Dashboard
int menu_is_dashboard(void);
void menu_enter_first_child(void);

// 页面绘制函数声明
void page_system_init(void);
void page_docker_init(void);
void page_network_init(void);
void page_system_init(void);
void page_env_init(void);
void page_terminal_init(void);

menu_item_t* page_terminal_create_menu(void);
menu_item_t *page_dashboard_create_menu(void);
menu_item_t *page_system_create_menu(void);
void page_home_draw(void);
menu_item_t *page_env_create_menu(void);
menu_item_t* page_network_create_menu(void);
menu_item_t* page_tools_create_menu(void);
menu_item_t* page_docker_create_menu(void);
void page_disk_draw(void);
void menu_enter(void);  // 新增：进入菜单列表
void menu_back(void);
// 在 menu.h 中添加
menu_item_t *menu_get_current(void);
#endif
