#ifndef PAGE_INTERFACE_H
#define PAGE_INTERFACE_H

// 事件类型
typedef enum {
    EV_UP,
    EV_DOWN,
    EV_ENTER,
    EV_BACK
} event_t;

// 页面接口
typedef struct page_interface {
    const char *name;
    void (*draw)(void);
    void (*handle_event)(event_t ev);
} page_interface_t;

// 统一注册接口（所有页面调用）
void page_register(const char *name, void (*draw)(void), void (*handle)(event_t));

// 根据名称获取接口（menu.c 调用）
page_interface_t* page_get_interface(const char *name);

#endif // PAGE_INTERFACE_H