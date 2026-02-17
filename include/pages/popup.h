#ifndef POPUP_H
#define POPUP_H
#include <page_interface.h>

// 弹窗配置宏（可根据需要调整）
#define POPUP_MAX_LEN 32  // 弹窗最大字符数
#define POPUP_X 10        // 弹窗左上角X坐标
#define POPUP_Y 20        // 弹窗左上角Y坐标
#define POPUP_W 108       // 弹窗宽度
#define POPUP_h 24        // 弹窗高度（现在和保护宏不重名）
#define CHAR_WIDTH 6      // 字符宽度

// ========== 对外暴露的核心接口（一行调用）==========
// 显示弹窗（msg：要提示的消息，比如"Low temperature!"）
void popup_show(const char *msg);

// 关闭弹窗（一般不用手动调，按确认键自动关）
void popup_hide(void);

// 弹窗事件处理（供menu.c调用，拦截按键）
int popup_handle_event(event_t ev);

// 弹窗绘制（供menu.c调用，覆盖在所有页面上层）
void popup_draw(void);

// 检查弹窗是否激活（可选）
int popup_is_active(void);

#endif // POPUP_H