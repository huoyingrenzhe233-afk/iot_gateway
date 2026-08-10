cmake_minimum_required(VERSION 3.16)
project(gateway LANGUAGES C CXX)
add_executable(gateway src/gateway/main.cpp third_party/mongoose.c)
target_include_directories(gateway PRIVATE
    src
    third_party
)
target_link_libraries(gateway PRIVATE
    pthread
)
