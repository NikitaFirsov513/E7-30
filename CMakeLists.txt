cmake_minimum_required(VERSION 3.30)

# Устанавливаем стандарт
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Включаем отладочную информацию
if(MSVC)
    # Для Debug сборки
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MDd /Zi /Ob0 /Od /RTC1")

    # Для Release сборки
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MD /O2 /DNDEBUG")

    # Опционально: для Edit & Continue (если нужно)
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /ZI")
endif()

project(E7-30-Control)

include_directories(${PROJECT_SOURCE_DIR}/libs/imgui)

find_package(OpenGL REQUIRED)

add_library(glfw STATIC IMPORTED)
set_target_properties(glfw PROPERTIES IMPORTED_LOCATION ${PROJECT_SOURCE_DIR}/libs/glfw/lib-vc2022/glfw3.lib)
target_include_directories(glfw INTERFACE ${PROJECT_SOURCE_DIR}/libs/glfw/include)

add_library(glad ${PROJECT_SOURCE_DIR}/libs/glad/src/glad.c)
target_include_directories(glad PUBLIC ${PROJECT_SOURCE_DIR}/libs/glad/include)

add_library(serial STATIC IMPORTED)
set_target_properties(serial PROPERTIES IMPORTED_LOCATION ${PROJECT_SOURCE_DIR}/libs/serial/lib/serial.lib)
target_include_directories(serial INTERFACE ${PROJECT_SOURCE_DIR}/libs/serial/include)


add_executable(E7-30-Control
    
    ${PROJECT_SOURCE_DIR}/src/main.cpp

    ${PROJECT_SOURCE_DIR}/src/device/E730Device.cpp
    ${PROJECT_SOURCE_DIR}/src/device/E730Device.h
    
    ${PROJECT_SOURCE_DIR}/src/com/SerialPort.h           
    ${PROJECT_SOURCE_DIR}/src/com/SerialPort.cpp

    ${PROJECT_SOURCE_DIR}/src/DeviceCommunicator.h           
    ${PROJECT_SOURCE_DIR}/src/DeviceCommunicator.cpp

    ${PROJECT_SOURCE_DIR}/src/sqlite/sqlite3.c           
    ${PROJECT_SOURCE_DIR}/src/sqlite/sqlite3.h

    ${PROJECT_SOURCE_DIR}/src/ui/ui.cpp           
    ${PROJECT_SOURCE_DIR}/src/ui/ui.h


    ${PROJECT_SOURCE_DIR}/src/imgui/imgui.cpp
    ${PROJECT_SOURCE_DIR}/src/imgui/imgui_demo.cpp
    ${PROJECT_SOURCE_DIR}/src/imgui/imgui_draw.cpp
    ${PROJECT_SOURCE_DIR}/src/imgui/imgui_impl_glfw.cpp
    ${PROJECT_SOURCE_DIR}/src/imgui/imgui_impl_opengl3.cpp
    ${PROJECT_SOURCE_DIR}/src/imgui/imgui_tables.cpp
    ${PROJECT_SOURCE_DIR}/src/imgui/imgui_widgets.cpp
    
    ${PROJECT_SOURCE_DIR}/src/stb_image/stb_image.h
    ${PROJECT_SOURCE_DIR}/src/stb_image/stb_image_write.h

)

target_link_libraries(E7-30-Control PRIVATE glfw glad serial opengl32.lib)