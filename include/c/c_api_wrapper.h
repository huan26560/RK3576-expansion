// c_api_wrapper.h
#ifndef C_API_WRAPPER_H
#define C_API_WRAPPER_H

// 确保 C++ 和 C 都能正确编译
#ifdef __cplusplus
extern "C" {
#endif

// ========== 系统头文件（避免依赖缺失） ==========
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>     // 确保所有 C 头文件中的 time_t 可用
#include <stdint.h>
#include <stdbool.h>

// ========== 包含所有 C 头文件 ==========
#include "db_helper.h"
#include "email_report.h"
#include "mqtt_client.h"
#include "network_monitor.h"
#include "system_monitor.h"
#include "threads.h"
#include "menu.h"
#include "hal/hal_gpio.h"
#include "hal/hal_dht11.h"
#include "hal/hal_oled.h"
#include "hal/hal_system.h"
#include "hal/hal_echo.h"


// ========== 声明可被 C 调用的 C++ 函数 ==========
void hello_from_cpp(void);

#ifdef __cplusplus
}
#endif

#endif // C_API_WRAPPER_H