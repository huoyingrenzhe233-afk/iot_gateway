cmake_minimum_required(VERSION 3.16)
project(gateway LANGUAGES C CXX)

# Export compile_commands.json for clangd (LSP: syntax check, go-to-def, completion)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 显式 C++14(与 cmake/toolchain-linaro.cmake 一致):compile_commands.json 会带
# -std=gnu++14,clangd 按此解析,和实际编译行为一致(消除"编译器过了 IDE 报错"的分歧)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(yaml-cpp REQUIRED)
add_executable(gateway
    src/gateway/main.cpp
    src/core/common/logger/logger.cpp
    src/core/common/config/config.cpp
    src/core/common/mqtt/mqtt_client.cpp
    src/core/control/control.cpp
    src/core/device/device.cpp
    src/core/device/device_registry.cpp
    src/core/rules/rule_engine.cpp
    src/core/camera/camera_manager.cpp
    src/core/storage/sqlite_store.cpp
    src/core/channel/zigbee_adapter.cpp
    src/core/channel/channel_manager.cpp
    third_party/mongoose.c
    third_party/sqlite3.c
)
target_include_directories(gateway PRIVATE
    src
    third_party
)
target_link_libraries(gateway PRIVATE
    pthread
    yaml-cpp
    dl
)

# ------------------------------------------------------------
# clangd 兼容:把 compile_commands.json 复制到项目根目录
# clangd 自动发现编译数据库时,只在"源文件的父目录链"里找
# compile_commands.json,不会进 build/ 子目录找。根目录放一份后,
# 任何编辑器(甚至旧版 clangd,不认 .clangd 配置)零配置即可正确解析,
# 消除"头文件找不到"类假错。
# 仅宿主机构建时复制(交叉编译的 build-arm 数据库含 linaro 编译器
# 路径,不适合喂给宿主机的 clangd)。
# ------------------------------------------------------------
if(NOT CMAKE_CROSSCOMPILING)
    add_custom_target(copy_compile_commands ALL
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/compile_commands.json"
            "${CMAKE_SOURCE_DIR}/compile_commands.json"
        COMMENT "Copying compile_commands.json to source root (for clangd auto-discovery)")

    # ------------------------------------------------------------
    # 回归测试:get_status_json() 的 last_report 真实新鲜度字段
    # 仅宿主机编译(交叉编译的板端不需要跑测试)。
    # 依赖:device.cpp(JSON 解析用 mongoose)+ logger.cpp
    # 运行:cmake --build build && (cd build && ctest --output-on-failure)
    # ------------------------------------------------------------
    enable_testing()
    add_executable(test_device_status
        tests/test_device_status.cpp
        src/core/device/device.cpp
        src/core/common/logger/logger.cpp
        third_party/mongoose.c
    )
    target_include_directories(test_device_status PRIVATE
        src
        third_party
    )
    target_link_libraries(test_device_status PRIVATE
        pthread
        dl
    )
    add_test(NAME device_status_last_report COMMAND test_device_status)
endif()
