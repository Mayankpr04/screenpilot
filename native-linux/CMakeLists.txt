cmake_minimum_required(VERSION 3.16)
project(ScreenPilotLinux VERSION 0.3.1 LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

add_executable(screenpilot-linux src/main.cpp)
target_compile_features(screenpilot-linux PRIVATE cxx_std_17)
target_compile_options(screenpilot-linux PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(screenpilot-linux PRIVATE PkgConfig::GTK3)
set_target_properties(screenpilot-linux PROPERTIES OUTPUT_NAME screenpilot)

install(TARGETS screenpilot-linux RUNTIME DESTINATION bin)
