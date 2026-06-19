#ifndef SCREENSAVER_H
#define SCREENSAVER_H

#include "hal_oled.h"  // OLED硬件层宏（OLED_WIDTH/OLED_HEIGHT/CHAR_WIDTH/CHAR_HEIGHT）
#include "page_interface.h"
// 对外接口声明
void screensaver_reset_idle(void);
int screensaver_handle_event(event_t ev);
void screensaver_draw(void);
int screensaver_is_active(void);
void screensaver_init(void);


#endif // SCREENSAVER_H