cmake_minimum_required(VERSION 3.24)
project(animparser)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- 1. Dependencies ---
include(FetchContent)

# A. GLFW
FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG 3.3.8
)
FetchContent_MakeAvailable(glfw)

# B. ImGui (Core + Backends)
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG docking # Use docking branch for latest features
)
FetchContent_MakeAvailable(imgui)

# C. ImGuiFileDialog
FetchContent_Declare(
        ImGuiFileDialog
        GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git
        GIT_TAG v0.6.7
)
FetchContent_MakeAvailable(ImGuiFileDialog)

# --- 2. Define Sources ---

# Define the ImGui sources we need to compile
set(IMGUI_SOURCES
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

# --- 3. Build the Executable ---
add_executable(animparser
        main.cpp
        ${IMGUI_SOURCES}
        ${ImGuiFileDialog_SOURCE_DIR}/ImGuiFileDialog.cpp
)

# --- 4. Include Directories ---
target_include_directories(animparser PRIVATE
        ${imgui_SOURCE_DIR}             # For imgui.h
        ${imgui_SOURCE_DIR}/backends    # For imgui_impl_glfw.h
        ${ImGuiFileDialog_SOURCE_DIR}   # For ImGuiFileDialog.h
)

# --- 5. Link Libraries ---
find_package(OpenGL REQUIRED)
target_link_libraries(animparser PRIVATE
        glfw
        OpenGL::GL
)