# ====================== 链接器优化 ======================
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fuse-ld=mold")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fuse-ld=mold")

cmake_minimum_required(VERSION 3.10)
project(expansion_ui C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ====================== 默认并行编译 ======================
set(CMAKE_BUILD_PARALLEL_LEVEL 16 CACHE STRING "Parallel build level" FORCE)

# ====================== 查找依赖 ======================
# 1. curl
find_package(CURL REQUIRED)

# 2. cJSON
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(CJSON QUIET libcjson)
endif()
if(NOT CJSON_FOUND)
    find_path(CJSON_INCLUDE_DIRS NAMES cjson/cJSON.h REQUIRED)
    find_library(CJSON_LIBRARIES NAMES cjson libcjson REQUIRED)
endif()

# 3. MySQL
if(PkgConfig_FOUND)
    pkg_check_modules(MYSQL QUIET mysqlclient)
endif()
if(NOT MYSQL_FOUND)
    find_path(MYSQL_INCLUDE_DIRS NAMES mysql/mysql.h mysql.h 
        PATHS /usr/include/mysql /usr/local/include/mysql REQUIRED)
    find_library(MYSQL_LIBRARIES NAMES mysqlclient 
        PATHS /usr/lib/aarch64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/local/lib REQUIRED)
endif()

# ====================== 头文件路径 ======================
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/c          # 核心头文件在这里（db_helper.h, menu.h...）
    ${CMAKE_SOURCE_DIR}/include/c/hal      # HAL 头文件（hal_dht11.h...）
    ${CMAKE_SOURCE_DIR}/include/c/pages    # 页面头文件（page_tools.h...）
    ${CMAKE_SOURCE_DIR}/include/cpp        # C++ 头文件
    ${CURL_INCLUDE_DIRS}
    ${CJSON_INCLUDE_DIRS}
    ${MYSQL_INCLUDE_DIRS}
)

# ====================== 自动收集源文件 ======================
# C 源文件
file(GLOB_RECURSE C_SOURCES
    "${CMAKE_SOURCE_DIR}/src/*.c"
    "${CMAKE_SOURCE_DIR}/src/hal/*.c"
    "${CMAKE_SOURCE_DIR}/src/pages/*.c"
)

# C++ 源文件
file(GLOB_RECURSE CPP_SOURCES
    "${CMAKE_SOURCE_DIR}/src/cpp/*.cpp"
)

# 合并所有源文件
set(SOURCES
    ${C_SOURCES}
    ${CPP_SOURCES}
)

# ====================== 可执行文件 ======================
add_executable(expansion_ui ${SOURCES})

# ====================== 链接库 ======================
target_link_libraries(expansion_ui PRIVATE 
    mosquitto 
    pthread 
    gpiod
    ${CURL_LIBRARIES}
    ${CJSON_LIBRARIES}
    ${MYSQL_LIBRARIES}
    xlsxwriter   
    m       
)

# 如果使用 mold 链接器失败，可以注释掉开头两行