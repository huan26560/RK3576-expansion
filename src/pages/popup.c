#include "popup.h"
#include "hal_oled.h"
#include <string.h>
#include <page_interface.h>


// 静态全局变量（仅本文件可见，封装性好）
static int popup_active = 0;          // 弹窗激活状态：0=关闭，1=激活
static char popup_msg[POPUP_MAX_LEN]; // 弹窗消息内容

// 绘制弹窗（内部实现，对外只暴露popup_draw）
static void draw_popup_ui(void)
{
    if (!popup_active) return;

    // 1. 绘制弹窗背景（黑色填充，覆盖底层内容）
    hal_oled_fill_rect(POPUP_X-1, POPUP_Y-1, POPUP_X+POPUP_W+1, POPUP_Y+POPUP_h+1, 0);
    // 2. 绘制弹窗白色边框
    hal_oled_rect(POPUP_X, POPUP_Y, POPUP_X+POPUP_W, POPUP_Y+POPUP_h, 1);
    // 3. 居中绘制弹窗消息
    int msg_len = strlen(popup_msg);
    int msg_x = POPUP_X + (POPUP_W - msg_len*CHAR_WIDTH)/2;
    hal_oled_string(msg_x, POPUP_Y + 6, popup_msg);
    // 4. 绘制关闭提示
    hal_oled_string(POPUP_X + 20, POPUP_Y + 16, "Press ENTER to close");
}

// ========== 对外接口实现 ==========
void popup_show(const char *msg)
{
    if (!msg || strlen(msg) >= POPUP_MAX_LEN) return;
    // 拷贝消息并激活弹窗
    strncpy(popup_msg, msg, POPUP_MAX_LEN-1);
    popup_msg[POPUP_MAX_LEN-1] = '\0';
    popup_active = 1;
}

void popup_hide(void)
{
    popup_active = 0;
}

int popup_handle_event(event_t ev)
{
    // 弹窗未激活时，返回0，交给其他模块处理事件
    if (!popup_active) return 0;

    // 弹窗激活时，仅响应确认键
    if (ev == EV_ENTER)
    {
        popup_hide();
    }
    // 返回1，表示事件已处理，屏蔽其他逻辑
    return 1;
}

void popup_draw(void)
{
    draw_popup_ui();
}

int popup_is_active(void)
{
    return popup_active;
}