cmake_minimum_required(VERSION 3.24)
project(ScreenPilotNative VERSION 0.2.0 LANGUAGES CXX RC)

add_executable(ScreenPilot WIN32
    src/main.cpp
    resources/screenpilot.rc
)

target_compile_features(ScreenPilot PRIVATE cxx_std_20)
target_compile_definitions(ScreenPilot PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(ScreenPilot PRIVATE
    Dxva2
    WbemUuid
    Ole32
    OleAut32
    Comctl32
    Shell32
    Dwmapi
)

if(MSVC)
    set_property(TARGET ScreenPilot PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(ScreenPilot PRIVATE /W4 /permissive- /EHsc /utf-8)
    target_link_options(ScreenPilot PRIVATE /SUBSYSTEM:WINDOWS)
endif()

install(TARGETS ScreenPilot RUNTIME DESTINATION .)
