cmake_minimum_required(VERSION 3.10)
project(CADViewer)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS Homebrew 前缀路径
if(APPLE)
    if(EXISTS /opt/homebrew)
        list(APPEND CMAKE_PREFIX_PATH /opt/homebrew/opt/glfw /opt/homebrew/opt/assimp /opt/homebrew/opt/glm)
    else()
        list(APPEND CMAKE_PREFIX_PATH /usr/local/opt/glfw /usr/local/opt/assimp /usr/local/opt/glm)
    endif()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations")
endif()

# 依赖项 
find_package(glfw3 REQUIRED)

# Assimp – 先尝试 pkg-config；失败则回退到 find_library
find_package(assimp QUIET)
if(NOT assimp_FOUND)
    find_path(ASSIMP_INCLUDE_DIR assimp/Importer.hpp
        PATHS /opt/homebrew/include /usr/local/include)
    find_library(ASSIMP_LIBRARY assimp
        PATHS /opt/homebrew/lib /usr/local/lib)
    if(NOT ASSIMP_INCLUDE_DIR OR NOT ASSIMP_LIBRARY)
        message(FATAL_ERROR "未找到 Assimp – 请执行 brew install assimp")
    endif()
endif()

# GLM 仅头文件库
find_path(GLM_INCLUDE_DIR glm/glm.hpp
    PATHS /opt/homebrew/include /usr/local/include)
if(NOT GLM_INCLUDE_DIR)
    message(FATAL_ERROR "未找到 GLM – 请执行 brew install glm")
endif()

# 包含路径 
include_directories(
    src
    ${ASSIMP_INCLUDE_DIR}
    ${GLM_INCLUDE_DIR}
    third_party/imgui
    third_party/imgui/backends
)

# 源文件
set(SOURCES
    src/main.cpp
    src/ModelLoader.cpp
    src/BezierSurface.cpp
    src/glad.c
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)

add_executable(CADViewer ${SOURCES})

# 链接库
if(assimp_FOUND)
    target_link_libraries(CADViewer glfw assimp::assimp
        "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
else()
    target_link_libraries(CADViewer glfw ${ASSIMP_LIBRARY}
        "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
endif()

# 抑制 Apple OpenGL 头文件的弃用警告
# 同时传递项目源码目录，以便应用能找到 assets/shaders
target_compile_definitions(CADViewer PRIVATE
    GL_SILENCE_DEPRECATION
    PROJECT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
