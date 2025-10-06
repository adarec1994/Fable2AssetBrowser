cmake_minimum_required(VERSION 3.20)
project(Fable_2_Asset_Browser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(APP_RES ${CMAKE_CURRENT_SOURCE_DIR}/fable.rc resource.h)

add_executable(Fable_2_Asset_Browser WIN32
        src/BNK_UI.cpp
        ${APP_RES}
)

find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    include(FetchContent)
    FetchContent_Declare(
            zlib
            GIT_REPOSITORY https://github.com/madler/zlib.git
            GIT_TAG v1.3.1
    )
    FetchContent_MakeAvailable(zlib)
    if(TARGET zlibstatic)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    elseif(TARGET zlib)
        add_library(ZLIB::ZLIB ALIAS zlib)
    endif()
endif()
target_link_libraries(Fable_2_Asset_Browser PRIVATE ZLIB::ZLIB)

include(FetchContent)
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.5
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
        imguifiledialog
        GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git
        GIT_TAG master
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imguifiledialog)

target_sources(Fable_2_Asset_Browser PRIVATE
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
        ${imguifiledialog_SOURCE_DIR}/ImGuiFileDialog.cpp
)

target_include_directories(Fable_2_Asset_Browser PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/towav
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/misc/cpp
        ${imgui_SOURCE_DIR}/backends
        ${imguifiledialog_SOURCE_DIR}
)

target_link_libraries(Fable_2_Asset_Browser PRIVATE d3d11 dxgi d3dcompiler dwmapi)

if(MINGW)
    target_link_options(Fable_2_Asset_Browser PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(Fable_2_Asset_Browser PRIVATE -lwinpthread)
endif()
