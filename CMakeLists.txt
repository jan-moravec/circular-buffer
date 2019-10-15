cmake_minimum_required(VERSION 3.5)

project(circular-buffer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(circular-buffer
    main.cpp
    circularbuffer.h
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE pthread
)
