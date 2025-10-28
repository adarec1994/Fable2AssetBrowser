cmake_minimum_required(VERSION 3.20)
project(Fable_2_Asset_Browser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(APP_RES ${CMAKE_CURRENT_SOURCE_DIR}/fable.rc resource.h)

set(APP_SOURCES
        src/main.cpp
        src/State.cpp
        src/Utils.cpp
        src/files.cpp
        src/TexParser.cpp
        src/ModelParser.cpp
        src/Progress.cpp
        src/Operations.cpp
        src/HexView.cpp
        src/UI_Main.cpp
        src/UI_Panels.cpp
        src/mdl_converter.cpp
        src/BNKCore.cpp
        src/audio.cpp
        src/ModelPreview.cpp
        src/ModelPreview.h
        src/play_audio.cpp
        src/play_audio.h
)

add_executable(Fable_2_Asset_Browser WIN32
        ${APP_SOURCES}
        ${APP_RES}
)

target_compile_definitions(Fable_2_Asset_Browser PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
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

FetchContent_Declare(
        imgui_hex_editor
        GIT_REPOSITORY https://github.com/Teselka/imgui_hex_editor.git
        GIT_TAG main
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui_hex_editor)

FetchContent_Declare(
        directxmath
        GIT_REPOSITORY https://github.com/microsoft/DirectXMath.git
        GIT_TAG main
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(directxmath)

FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

target_sources(Fable_2_Asset_Browser PRIVATE
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
        ${imguifiledialog_SOURCE_DIR}/ImGuiFileDialog.cpp
        ${imgui_hex_editor_SOURCE_DIR}/imgui_hex.cpp
)

target_include_directories(Fable_2_Asset_Browser PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/towav
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/misc/cpp
        ${imgui_SOURCE_DIR}/backends
        ${imguifiledialog_SOURCE_DIR}
        ${imgui_hex_editor_SOURCE_DIR}
        ${directxmath_SOURCE_DIR}/Inc
        ${stb_SOURCE_DIR}
)

target_link_libraries(Fable_2_Asset_Browser PRIVATE d3d11 dxgi d3dcompiler dwmapi winmm)

if(MINGW)
    target_link_options(Fable_2_Asset_Browser PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(Fable_2_Asset_Browser PRIVATE -lwinpthread)
endif()