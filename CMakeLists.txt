cmake_minimum_required(VERSION 3.16)
project(gateway LANGUAGES C CXX)
find_package(yaml-cpp REQUIRED)
add_executable(gateway
    src/gateway/main.cpp
    src/core/common/logger/logger.cpp
    src/core/common/config/config.cpp
    third_party/mongoose.c
)
target_include_directories(gateway PRIVATE
    src
    third_party
)
target_link_libraries(gateway PRIVATE
    pthread
    yaml-cpp
)
