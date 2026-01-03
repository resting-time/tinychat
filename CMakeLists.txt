cmake_minimum_required(VERSION 3.10)
project(TinyChat LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(Protobuf REQUIRED)
protobuf_generate_cpp(PROTO_SRC PROTO_HDR msg.proto)

add_executable(server main.cpp ${PROTO_SRC})
add_executable(client client.cpp ${PROTO_SRC})

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED hiredis)

target_include_directories(server PRIVATE ${HIREDIS_INCLUDE_DIRS})
target_link_libraries(server ${Protobuf_LIBRARIES} ${HIREDIS_LIBRARIES} Threads::Threads mysqlcppconn)
target_link_libraries(client ${Protobuf_LIBRARIES} Threads::Threads)
