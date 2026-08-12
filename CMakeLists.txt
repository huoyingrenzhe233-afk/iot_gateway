cmake_minimum_required(VERSION 3.16)
project(gateway LANGUAGES C CXX)

# Export compile_commands.json for clangd (LSP: syntax check, go-to-def, completion)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(yaml-cpp REQUIRED)
add_executable(gateway
    src/gateway/main.cpp
    src/core/common/logger/logger.cpp
    src/core/common/config/config.cpp
    src/core/common/mqtt/mqtt_client.cpp
    src/core/control/control.cpp
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
