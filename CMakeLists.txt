cmake_minimum_required(VERSION 3.10)
project(Test)

# 1. 自动查找 MySQL 库（Linux 标准路径）
find_library(MYSQL_LIB mysqlclient REQUIRED)

find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h)
find_library(HIREDIS_LIBRARY NAMES hiredis)

# 2. 添加头文件目录
include_directories(include)
include_directories(/usr/include/mysql) 

# 3. 查找线程库
find_package(Threads REQUIRED)

# 4. 源码搜索
aux_source_directory(./src/server SV)
aux_source_directory(./src/client CL)

# 5. 配置 Server
add_executable(server ${SV})
target_link_libraries(server
    Threads::Threads 
    ${MYSQL_LIB} 
    ${HIREDIS_LIBRARY}
)

# 6. 配置 Client
add_executable(client ${CL})