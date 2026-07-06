cmake_minimum_required(VERSION 3.20)
project(Fable_2_Asset_Browser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

set(APP_SOURCES
        src/main.cpp
        src/Utilities/State.cpp
        src/Utilities/Utils.cpp
        src/Utilities/Files.cpp
        src/Utilities/AutoPilot.cpp
        src/textures/TexParser.cpp
        src/textures/LhTexCodec.cpp
        src/animations/AnimBank.cpp
        src/animations/AnimDataFile.cpp
        src/animations/AnimDecoder.cpp
        src/animations/Locomotion.cpp
        src/animations/AnimPlayer.cpp
        src/textures/export/TextureExport.cpp
        src/textures/export/PngExport.cpp
        src/textures/export/JpgExport.cpp
        src/textures/export/TiffExport.cpp
        src/textures/export/DdsExport.cpp
        src/MDL/ModelParser.cpp
        src/MDL/MdlFbxExport.cpp
        src/MDL/MdlTexExport.cpp
        src/Utilities/Progress.cpp
        src/Utilities/operations.cpp
        src/UI/HexView.cpp
        src/UI/OutputLog.cpp
        src/UI/IconButton.cpp
        src/UI/UI_Main.cpp
        src/UI/Panels/Selection.cpp
        src/UI/Panels/TreeBuilder.cpp
        src/UI/Panels/TreeRender.cpp
        src/UI/Panels/LeftPanel.cpp
        src/UI/Panels/FileTable.cpp
        src/UI/Panels/FolderDialog.cpp
        src/UI/Panels/RightPanel.cpp
        src/UI/Panels/PendingLoads.cpp
        src/UI/AudioPlayerWindow.cpp
        src/UI/Layout/LoadingScreen.cpp
        src/UI/Layout/MainLayout.cpp
        src/UI/Layout/RenderPanel.cpp
        src/UI/About/AboutWindow.cpp
        src/Splashscreen/Splashscreen.cpp
        src/Splashscreen/SplashSparkles.cpp
        src/Splashscreen/LogoMap.cpp
        src/Splashscreen/IconFont.cpp
        src/ISO/IsoExtract.cpp
        src/ISO/IsoMount.cpp
        src/ISO/IsoDump.cpp
        src/MDL/mdl_converter.cpp
        src/Level/LevelLoader.cpp
        src/Level/LevelLoader.h
        src/Level/LevelExport.cpp
        src/Level/LevelExport.h
        src/Level/HeightfieldLoader.cpp
        src/Level/HeightfieldLoader.h
        src/Level/TextureAtlasDecoder.cpp
        src/Level/TextureAtlasDecoder.h
        src/Level/EhfPalette.cpp
        src/Level/EhfPalette.h
        src/Level/ShaderBankFile.cpp
        src/Level/ShaderBankFile.h
        src/Level/TerrainTextureRegistry.cpp
        src/Level/TerrainTextureRegistry.h
        src/Level/EhfChunkParser.cpp
        src/Level/EhfChunkParser.h
        src/Level/EhfLodThumbnails.cpp
        src/Level/EhfLodThumbnails.h
        src/Level/TerrainSplat.cpp
        src/Level/TerrainSplat.h
        src/Level/TerrainEdit.cpp
        src/Level/TerrainEdit.h
        src/Level/VfsConfig.cpp
        src/Level/VfsConfig.h
        src/Level/GdbModelHashlist.cpp
        src/Level/GdbModelHashlist.h
        src/Level/GdbParser.cpp
        src/Level/GdbParser.h
        src/Level/WaterParser.cpp
        src/Level/WaterParser.h
        src/Havok/HavokPackfileReader.cpp
        src/Havok/HavokPackfileReader.h
        src/BNKCore.cpp
        src/Audio/AudioPlayer.cpp
        src/Audio/XmaDecoder.cpp
        src/Audio/MfAudioEncoder.cpp
        src/Audio/Xma2Codec/XmaSupport.cpp
        src/Audio/Xma2Codec/XmaParser.cpp
        src/Audio/Xma2Codec/XmaWmaCore.cpp
        src/Audio/Xma2Codec/XmaBitstream.cpp
        src/Audio/Xma2Codec/XmaTx.cpp
        src/Audio/Xma2Codec/XmaWmaProDecoder.cpp
        src/UI/ModelPreview.cpp
        src/UI/ModelPreview.h
        src/Lua.cpp
        src/Lua.h
        src/lua/lua_decompile.h
        src/lua/lua_parse.cpp
        src/lua/lua_decompile.cpp
        src/lua/lua_decompiler.cpp
)

if(WIN32)
    set(APP_RES ${CMAKE_CURRENT_SOURCE_DIR}/fable.rc resource.h)
    list(APPEND APP_SOURCES
            src/Audio/play_audio.cpp
            src/Audio/play_audio.h
    )
    add_executable(Fable_2_Asset_Browser WIN32
            ${APP_SOURCES}
            ${APP_RES}
    )
else()
    add_executable(Fable_2_Asset_Browser
            ${APP_SOURCES}
    )
endif()

target_compile_definitions(Fable_2_Asset_Browser PRIVATE
        _CRT_SECURE_NO_WARNINGS
)

if(WIN32)
    target_compile_definitions(Fable_2_Asset_Browser PRIVATE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
    )
endif()

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
if(TARGET ImGuiFileDialog)
    target_include_directories(ImGuiFileDialog PRIVATE
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/misc/cpp
            ${imgui_SOURCE_DIR}/backends)
endif()

FetchContent_Declare(
        imgui_hex_editor
        GIT_REPOSITORY https://github.com/Teselka/imgui_hex_editor.git
        GIT_TAG main
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui_hex_editor)

FetchContent_Declare(
        stb
        GIT_REPOSITORY https://github.com/nothings/stb.git
        GIT_TAG master
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

FetchContent_Declare(
        iconfontcpp
        GIT_REPOSITORY https://github.com/juliettef/IconFontCppHeaders.git
        GIT_TAG main
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(iconfontcpp)

FetchContent_Declare(
        fontawesome
        GIT_REPOSITORY https://github.com/FortAwesome/Font-Awesome.git
        GIT_TAG 6.x
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(fontawesome)

FetchContent_Declare(
        miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 0.11.21
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(miniaudio)

if(WIN32)
    FetchContent_Declare(
            directxmath
            GIT_REPOSITORY https://github.com/microsoft/DirectXMath.git
            GIT_TAG main
            GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(directxmath)

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
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/misc/cpp
            ${imgui_SOURCE_DIR}/backends
            ${imguifiledialog_SOURCE_DIR}
            ${imgui_hex_editor_SOURCE_DIR}
            ${directxmath_SOURCE_DIR}/Inc
            ${stb_SOURCE_DIR}
            ${iconfontcpp_SOURCE_DIR}
            ${miniaudio_SOURCE_DIR}
    )

    set(FA_TTF_SOURCE "${fontawesome_SOURCE_DIR}/webfonts/fa-solid-900.ttf")
    set(FA_TTF_STAGED "${CMAKE_CURRENT_SOURCE_DIR}/include/image/fa-solid-900.ttf")
    if(EXISTS "${FA_TTF_SOURCE}")
        configure_file("${FA_TTF_SOURCE}" "${FA_TTF_STAGED}" COPYONLY)
    endif()

    target_link_libraries(Fable_2_Asset_Browser PRIVATE
            d3d11 dxgi d3dcompiler dwmapi winmm
            mfreadwrite mfplat mfuuid strmiids)
else()
    find_package(OpenGL REQUIRED)
    find_package(glfw3 REQUIRED)
    find_package(GLEW REQUIRED)

    target_sources(Fable_2_Asset_Browser PRIVATE
            ${imgui_SOURCE_DIR}/imgui.cpp
            ${imgui_SOURCE_DIR}/imgui_draw.cpp
            ${imgui_SOURCE_DIR}/imgui_tables.cpp
            ${imgui_SOURCE_DIR}/imgui_widgets.cpp
            ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
            ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
            ${imguifiledialog_SOURCE_DIR}/ImGuiFileDialog.cpp
            ${imgui_hex_editor_SOURCE_DIR}/imgui_hex.cpp
    )

    target_include_directories(Fable_2_Asset_Browser PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${imgui_SOURCE_DIR}
            ${imgui_SOURCE_DIR}/misc/cpp
            ${imgui_SOURCE_DIR}/backends
            ${imguifiledialog_SOURCE_DIR}
            ${imgui_hex_editor_SOURCE_DIR}
            ${iconfontcpp_SOURCE_DIR}
            ${stb_SOURCE_DIR}
            ${miniaudio_SOURCE_DIR}
            ${OPENGL_INCLUDE_DIR}
            ${GLEW_INCLUDE_DIRS}
    )

    target_link_libraries(Fable_2_Asset_Browser PRIVATE
            OpenGL::GL
            glfw
            GLEW::GLEW
            pthread
    )
endif()

if(MINGW)
    target_link_options(Fable_2_Asset_Browser PRIVATE -static -static-libgcc -static-libstdc++)
    target_link_libraries(Fable_2_Asset_Browser PRIVATE -lwinpthread)
endif()
