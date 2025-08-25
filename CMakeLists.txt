cmake_minimum_required(VERSION 3.26)
project(MySQLXTest)

set(CMAKE_CXX_STANDARD 17)

include_directories(/usr/local/opt/mysql-connector-c++/include)
link_directories(/usr/local/opt/mysql-connector-c++/lib)

add_executable(MySQLXTest main.cpp)

target_link_libraries(MySQLXTest mysqlcppconnx)  # ✅ FIXED
