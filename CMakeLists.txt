cmake_minimum_required(VERSION 3.10)
# 明确指定C语言项目，避免编译器兼容问题
project(expansion_ui C)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ====================== 优化：兼容式依赖查找，避免pkg-config报错 ======================
# 1. 查找curl库（HTTP请求必需，强依赖）
find_package(CURL REQUIRED)

# 2. 兼容式查找cJSON库：优先用pkg-config，失败则直接查找库文件
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(CJSON QUIET libcjson)
endif()

# 若pkg-config查找失败，直接手动查找cJSON头文件和库
if(NOT CJSON_FOUND)
    find_path(CJSON_INCLUDE_DIRS NAMES cjson/cJSON.h REQUIRED)
    find_library(CJSON_LIBRARIES NAMES cjson libcjson REQUIRED)
endif()

# 原有头文件目录（完全保留，无任何修改）
include_directories(/home/cat/expansion/include)
include_directories(/home/cat/expansion/include/hal)
include_directories(/home/cat/expansion/include/pages)
# 新增：依赖库头文件目录
include_directories(${CURL_INCLUDE_DIRS})
include_directories(${CJSON_INCLUDE_DIRS})

# 原有源文件列表（完全保留，无任何修改）
set(SOURCES
    /home/cat/expansion/src/main.c
    /home/cat/expansion/src/menu.c
    /home/cat/expansion/src/hal/hal_dht11.c
    /home/cat/expansion/src/thread.c
    /home/cat/expansion/src/mqtt_client.c
    /home/cat/expansion/src/hal/hal_gpio.c
    /home/cat/expansion/src/hal/hal_oled.c
    /home/cat/expansion/src/hal/hal_system.c
    /home/cat/expansion/src/pages/page_dashboard.c
    /home/cat/expansion/src/pages/page_tools.c
    /home/cat/expansion/src/pages/page_system.c
    /home/cat/expansion/src/pages/page_interface.c
    /home/cat/expansion/src/pages/page_env.c
    /home/cat/expansion/src/pages/page_docker.c
    /home/cat/expansion/src/pages/page_network.c
    /home/cat/expansion/src/pages/page_terminal.c
    /home/cat/expansion/src/pages/page_screensaver.c
    /home/cat/expansion/src/network_monitor.c
    /home/cat/expansion/src/system_monitor.c
)

add_executable(expansion_ui ${SOURCES})

# 原有链接库完全保留，新增curl和cJSON链接
target_link_libraries(expansion_ui PRIVATE 
    mosquitto 
    pthread 
    gpiod
    ${CURL_LIBRARIES}
    ${CJSON_LIBRARIES}
)