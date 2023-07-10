cmake_minimum_required(VERSION 3.10)

project(HttpServer
    DESCRIPTION "A small web server that supports HTTP/1.1"
    LANGUAGES CXX)

set(SRC_DIR src)
set(BUILD_DIR build)

file(COPY ${SRC_DIR}/index.html DESTINATION ${BUILD_DIR})

add_executable(HttpServer
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/http_server.cpp
    ${SRC_DIR}/http_message.cpp
)

set_target_properties(HttpServer PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${BUILD_DIR}/")
