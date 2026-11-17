cmake_minimum_required(VERSION 3.20)
project(Polyhymnia VERSION 0.1.0 LANGUAGES C CXX)
# Polyhymnia: Atelier's music studio (DAW). SDL2, OpenGL 3, Dear ImGui, miniaudio.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(EXT ${CMAKE_SOURCE_DIR}/external)
if(NOT EXISTS ${EXT}/miniaudio.h)
    file(DOWNLOAD https://raw.githubusercontent.com/mackron/miniaudio/0.11.21/miniaudio.h ${EXT}/miniaudio.h)
endif()
if(NOT EXISTS ${EXT}/stb_vorbis.c)
    file(DOWNLOAD https://raw.githubusercontent.com/nothings/stb/master/stb_vorbis.c ${EXT}/stb_vorbis.c)
endif()

include(FetchContent)
if(EXISTS ${EXT}/imgui-1.90.4/imgui.h)
    set(FETCHCONTENT_SOURCE_DIR_IMGUI ${EXT}/imgui-1.90.4)
endif()
FetchContent_Declare(imgui URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.4.tar.gz)
FetchContent_MakeAvailable(imgui)

if(WIN32 AND NOT SDL2_DIR AND EXISTS ${EXT}/SDL2-2.30.9/cmake)
    set(SDL2_DIR ${EXT}/SDL2-2.30.9/cmake)
endif()
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC SDL2::SDL2 OpenGL::GL)

set(CORE_SOURCES src/lang.cpp src/splash.cpp src/audio.cpp src/tempo.cpp src/tempo_map.cpp src/timecode.cpp src/time_wide.cpp
    src/session.cpp src/project.cpp src/mix.cpp src/playback.cpp src/edit.cpp src/edit_drag.cpp src/clip_ops.cpp src/time_grid.cpp src/time_format.cpp
    src/spot_dialog.cpp src/engine_edit.cpp src/engine_fix.cpp src/import_audio.cpp src/clips_panel.cpp
    src/consolidate.cpp src/editor.cpp src/undo.cpp src/test_runner.cpp tests/test_time_grid.cpp tests/test_engine_edit.cpp tests/test_clip_model.cpp
    tests/test_cmd_edit_fix.cpp tests/test_cmd_edit_more.cpp tests/test_cmd_clip.cpp tests/test_cmd_clip_more.cpp tests/test_import_audio.cpp)
set(GUI_SOURCES src/main.cpp src/app.cpp)
if(WIN32 AND EXISTS ${CMAKE_SOURCE_DIR}/src/app.rc)
    list(APPEND GUI_SOURCES src/app.rc)
endif()

add_executable(Polyhymnia WIN32 ${GUI_SOURCES} ${CORE_SOURCES})
target_include_directories(Polyhymnia PRIVATE ${EXT} src)
target_link_libraries(Polyhymnia PRIVATE imgui)
target_compile_definitions(Polyhymnia PRIVATE POLYHYMNIA_VERSION="${PROJECT_VERSION}" NOMINMAX)
if(TARGET SDL2::SDL2main)
    target_link_libraries(Polyhymnia PRIVATE SDL2::SDL2main)
endif()
if(MSVC)
    target_compile_options(Polyhymnia PRIVATE /W3 /utf-8 /O2 /D_CRT_SECURE_NO_WARNINGS)
    target_link_options(Polyhymnia PRIVATE /STACK:33554432)
else()
    target_link_libraries(Polyhymnia PRIVATE dl pthread)
endif()

add_custom_command(TARGET Polyhymnia POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:Polyhymnia>/assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/lang $<TARGET_FILE_DIR:Polyhymnia>/lang)
if(WIN32)
    add_custom_command(TARGET Polyhymnia POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:Polyhymnia>)
endif()