cmake_minimum_required(VERSION 3.10)
project(expansion_ui)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(/home/cat/expansion/include)
include_directories(/home/cat/expansion/include/hal)
include_directories(/home/cat/expansion/include/pages)


# 使用绝对路径（避免任何拼接错误）
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
    /home/cat/expansion/src/pages/popup.c

    /home/cat/expansion/src/network_monitor.c
    /home/cat/expansion/src/system_monitor.c
    
    # /home/cat/expansion/pages/page_disk.c
   
)

add_executable(expansion_ui ${SOURCES})
target_link_libraries(expansion_ui PRIVATE mosquitto pthread gpiod)