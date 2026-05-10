cmake_minimum_required(VERSION 3.20)
project(Fable_2_Asset_Browser CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Force the Release CRT (/MD) for every configuration. The FFmpeg static
# libs we ship are built with /MD; mixing them with /MDd in Debug
# produces LNK2038 (RuntimeLibrary mismatch). Losing /MDd costs us only
# a few CRT-level debug asserts; the C++ debugger still works fine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

set(APP_SOURCES
        src/main.cpp
        src/Utilities/State.cpp
        src/Utilities/Utils.cpp
        src/Utilities/Files.cpp
        src/textures/TexParser.cpp
        src/textures/LhTexCodec.cpp
        src/animations/AnimBank.cpp
        src/animations/AnimDataFile.cpp
        src/animations/AnimDecoder.cpp
        src/animations/AnimPlayer.cpp
        src/textures/export/TextureExport.cpp
        src/textures/export/PngExport.cpp
        src/textures/export/JpgExport.cpp
        src/textures/export/TiffExport.cpp
        src/textures/export/DdsExport.cpp
        src/MDL/ModelParser.cpp
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
        src/BNKCore.cpp
        src/Audio/AudioPlayer.cpp
        src/Audio/XmaDecoder.cpp
        src/Audio/MfAudioEncoder.cpp
        # Minimal override of libavcodec/allcodecs.obj — defines codec_list
        # with only XMA1/XMA2/WMAPro so the linker drops every other codec
        # via /OPT:REF instead of dragging the full FFmpeg surface (~25MB)
        # into the exe. See the file header for the link-order trick.
        src/Audio/Xma2Codec/xma_codec_list.cpp
        src/UI/ModelPreview.cpp
        src/UI/ModelPreview.h
        src/Lua.cpp
        src/Lua.h
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

# Font Awesome 6 — IconFontCppHeaders for the icon code points + the
# Solid font itself. Pulled separately because the headers repo doesn't
# carry the font binaries.
FetchContent_Declare(
        iconfontcpp
        GIT_REPOSITORY https://github.com/juliettef/IconFontCppHeaders.git
        GIT_TAG main
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(iconfontcpp)

# fontawesome-free, "free for desktop", contains the .ttf files we need
# under otfs/ (Font Awesome 6 Free-Solid-900.otf). We fetch master and
# pick out the otf at runtime/build-time.
FetchContent_Declare(
        fontawesome
        GIT_REPOSITORY https://github.com/FortAwesome/Font-Awesome.git
        GIT_TAG 6.x
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(fontawesome)

# miniaudio: single-header public-domain audio output library. We use it
# as the playback backend for the in-app audio player so we don't have to
# write our own WASAPI/CoreAudio/ALSA wrapper.
FetchContent_Declare(
        miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 0.11.21
        GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(miniaudio)

# FFmpeg (libavcodec) for XMA2 decoding. The Xbox 360 audio format in
# Fable 2's BNKs is hardware-decoded on console; there is no software
# decoder in the game binary. FFmpeg's libavcodec is the de-facto solution.
#
# On Windows we expect prebuilt MSVC libraries and headers under
# include/ffmpeg/ (.lib + include/), shipped with the project. The user
# can drop ShiftMediaProject prebuilts there. If absent, we fall back to
# the legacy towav.exe path so the rest of the app still builds.
set(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/include/ffmpeg" CACHE PATH "FFmpeg prebuilt root")
set(FFMPEG_AVAILABLE FALSE)
if(EXISTS "${FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    # Prefer the static archives (libavcodec.lib) so we can link FFmpeg
    # directly into the exe without shipping ~24 MB of DLLs alongside.
    # The linker (/OPT:REF) drops every codec we don't reference, so the
    # final exe footprint of the FFmpeg surface is roughly just the
    # XMA2/WMA Pro decode path + math/transform helpers.
    set(_FFMPEG_LIB_PATHS "${FFMPEG_ROOT}/lib" "${FFMPEG_ROOT}/lib/x86" "${FFMPEG_ROOT}/lib/x64")
    find_library(AVCODEC_LIB    NAMES libavcodec    avcodec     PATHS ${_FFMPEG_LIB_PATHS} NO_DEFAULT_PATH)
    find_library(AVUTIL_LIB     NAMES libavutil     avutil      PATHS ${_FFMPEG_LIB_PATHS} NO_DEFAULT_PATH)
    find_library(SWRESAMPLE_LIB NAMES libswresample swresample  PATHS ${_FFMPEG_LIB_PATHS} NO_DEFAULT_PATH)
    if(AVCODEC_LIB AND AVUTIL_LIB AND SWRESAMPLE_LIB)
        set(FFMPEG_AVAILABLE TRUE)
        message(STATUS "FFmpeg found at ${FFMPEG_ROOT} -- enabling XMA2 decoder (static link)")
        message(STATUS "  avcodec    : ${AVCODEC_LIB}")
        message(STATUS "  avutil     : ${AVUTIL_LIB}")
        message(STATUS "  swresample : ${SWRESAMPLE_LIB}")
    endif()
endif()
if(NOT FFMPEG_AVAILABLE)
    message(STATUS "FFmpeg prebuilts not found at ${FFMPEG_ROOT} -- audio player will only play uncompressed PCM. Drop libavcodec/avutil/swresample (.lib + include/) under include/ffmpeg/ to enable XMA2 decoding.")
endif()

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

    # Stage the FontAwesome 6 Solid ttf at a stable path that fable.rc
    # references (it goes in next to the splash images so it can be
    # bundled into the exe as RCDATA).
    set(FA_TTF_SOURCE "${fontawesome_SOURCE_DIR}/webfonts/fa-solid-900.ttf")
    set(FA_TTF_STAGED "${CMAKE_CURRENT_SOURCE_DIR}/include/image/fa-solid-900.ttf")
    if(EXISTS "${FA_TTF_SOURCE}")
        configure_file("${FA_TTF_SOURCE}" "${FA_TTF_STAGED}" COPYONLY)
    endif()

    # mfreadwrite gives us MFCreateSinkWriterFromURL, the entry point for
    # the PCM→MP3/AAC encode pipeline in MfAudioEncoder. mfplat / mfuuid
    # were already linked further down via the FFmpeg dependency block;
    # mfreadwrite is the missing piece to make the SinkWriter API
    # resolve at link time.
    target_link_libraries(Fable_2_Asset_Browser PRIVATE
            d3d11 dxgi d3dcompiler dwmapi winmm mfreadwrite)

    if(FFMPEG_AVAILABLE)
        target_compile_definitions(Fable_2_Asset_Browser PRIVATE F2_HAVE_FFMPEG=1)
        target_include_directories(Fable_2_Asset_Browser PRIVATE "${FFMPEG_ROOT}/include")
        target_link_libraries(Fable_2_Asset_Browser PRIVATE
                "${AVCODEC_LIB}" "${AVUTIL_LIB}" "${SWRESAMPLE_LIB}"
                # System libs that the static FFmpeg builds depend on.
                bcrypt secur32 ws2_32 mfplat mfuuid strmiids)

        # /OPT:REF drops every COMDAT (function/global) the linker can prove
        # is unreachable from the entry point. /OPT:ICF folds identical
        # COMDATs into one. With FFmpeg statically linked these are the
        # whole game: avcodec.lib carries hundreds of codecs as static
        # COMDATs and we only call the XMA2/WMA Pro path. Without these
        # flags the exe ends up enormous; with them it's a few MB.
        #
        # /INCREMENTAL is the Debug default; it disables /OPT:REF/ICF
        # because those rewrite already-laid-out sections. Force it off
        # so the size optimizations actually take effect — we trade
        # incremental-link speed for an exe that's an order of magnitude
        # smaller.
        target_link_options(Fable_2_Asset_Browser PRIVATE
                "/INCREMENTAL:NO" "/OPT:REF" "/OPT:ICF")
    endif()
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