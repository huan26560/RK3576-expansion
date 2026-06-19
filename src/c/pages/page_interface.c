// 删除所有 extern 声明和 if-else 链
// 替换为以下完整代码

#include "page_interface.h"
#include <string.h>
#include <stdlib.h>

#define MAX_PAGES 32

static struct {
    const char *name;
    void (*draw)(void);
    void (*handle)(event_t);
} registry[MAX_PAGES];
static int reg_count = 0;

void page_register(const char *name, void (*draw)(void), void (*handle)(event_t))
{
    if (reg_count < MAX_PAGES && name && draw && handle) {
        registry[reg_count].name = name;
        registry[reg_count].draw = draw;
        registry[reg_count].handle = handle;
        reg_count++;
    }
}

page_interface_t* page_get_interface(const char *name)
{
    static page_interface_t temp;
    for (int i = 0; i < reg_count; i++) {
        if (strcmp(registry[i].name, name) == 0) {
            temp.name = registry[i].name;
            temp.draw = registry[i].draw;
            temp.handle_event = registry[i].handle;
            return &temp;
        }
    }
    return NULL;
}