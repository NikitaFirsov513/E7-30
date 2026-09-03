cmake_minimum_required(VERSION 3.20)
project(E730Control LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenGL REQUIRED)

add_library(glfw STATIC IMPORTED GLOBAL)
set_target_properties(glfw PROPERTIES
    IMPORTED_LOCATION "${PROJECT_SOURCE_DIR}/libs/glfw/lib-vc2022/glfw3.lib")
target_include_directories(glfw INTERFACE "${PROJECT_SOURCE_DIR}/libs/glfw/include")

add_library(glad STATIC "${PROJECT_SOURCE_DIR}/libs/glad/src/glad.c")
target_include_directories(glad PUBLIC "${PROJECT_SOURCE_DIR}/libs/glad/include")

add_library(e730_sqlite STATIC "${PROJECT_SOURCE_DIR}/src/sqlite/sqlite3.c")
target_include_directories(e730_sqlite PUBLIC "${PROJECT_SOURCE_DIR}/src/sqlite")

add_library(e730_core STATIC
    src/device/E730Device.cpp
    src/device/E730Device.h
    src/protocol/E730Protocol.cpp
    src/protocol/E730Protocol.h
    src/com/SerialPort.cpp
    src/com/SerialPort.h
    src/storage/DataStore.cpp
    src/storage/DataStore.h
    src/DeviceCommunicator.cpp
    src/DeviceCommunicator.h)
target_include_directories(e730_core PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(e730_core PUBLIC e730_sqlite)
target_compile_definitions(e730_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

add_executable(E7-30-Control
    src/main.cpp
    src/ui/ui.cpp
    src/ui/ui.h
    src/imgui/imgui.cpp
    src/imgui/imgui_demo.cpp
    src/imgui/imgui_draw.cpp
    src/imgui/imgui_impl_glfw.cpp
    src/imgui/imgui_impl_opengl3.cpp
    src/imgui/imgui_tables.cpp
    src/imgui/imgui_widgets.cpp)
target_include_directories(E7-30-Control PRIVATE
    "${PROJECT_SOURCE_DIR}/libs/imgui/include"
    "${PROJECT_SOURCE_DIR}/libs/imgui")
target_link_libraries(E7-30-Control PRIVATE
    e730_core glfw glad OpenGL::GL setupapi)

if(MSVC)
    target_compile_options(e730_core PRIVATE /W4 /WX /permissive- /sdl /utf-8 /EHsc)
    target_compile_options(E7-30-Control PRIVATE /W4 /WX /permissive- /sdl /utf-8 /EHsc)
    set_source_files_properties(
        src/imgui/imgui.cpp
        src/imgui/imgui_demo.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_impl_glfw.cpp
        src/imgui/imgui_impl_opengl3.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp
        PROPERTIES COMPILE_OPTIONS "/W0;/WX-")
else()
    target_compile_options(e730_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    target_compile_options(E7-30-Control PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(e730_tests tests/e730_tests.cpp)
    target_link_libraries(e730_tests PRIVATE e730_core)
    if(MSVC)
        target_compile_options(e730_tests PRIVATE /W4 /WX /permissive- /sdl /utf-8 /EHsc)
    else()
        target_compile_options(e730_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    endif()
    add_test(NAME e730_tests COMMAND e730_tests)
endif()
