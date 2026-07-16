cmake_minimum_required(VERSION 3.20)
project(Fable_2_Asset_Browser CXX)

# Dependencies are pinned to explicit revisions below. Once populated, avoid
# contacting every upstream repository during unrelated source changes.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

option(FABLE_ENABLE_RECONSTRUCTED_CLOUD_PREVIEW
       "Enable the non-authoritative handwritten D3D11 cloud preview"
       ON)
option(FABLE_ENABLE_RECONSTRUCTED_SKY_PREVIEW
       "Enable the non-authoritative handwritten D3D11 sky preview"
       OFF)

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
        src/Level/Core/LevelLoader.cpp
        src/Level/Core/LevelLoader.h
        src/Level/Loading/LevelBinaryReader.h
        src/Level/Loading/EngineLevelParser.cpp
        src/Level/Loading/LevelCatalogLoader.cpp
        src/Level/Loading/LevelCatalogLoaderInternal.h
        src/Level/Loading/LevelTerrainLoader.cpp
        src/Level/Loading/LevelTerrainMesh.cpp
        src/Level/Loading/LevelTerrainLoaderInternal.h
        src/Level/Loading/Stages/LoadContainers.inl
        src/Level/Loading/Stages/LoadEffects.inl
        src/Level/Loading/Stages/LoadEntities.inl
        src/Level/Loading/Stages/LoadEnvironment.inl
        src/Level/Loading/Stages/LoadProps.inl
        src/Level/Loading/Stages/LevelPropSupport.inl
        src/Level/Core/LevelExport.cpp
        src/Level/Core/LevelExport.h
        src/Level/Terrain/HeightfieldLoader.cpp
        src/Level/Terrain/HeightfieldLoader.h
        src/Level/Terrain/TextureAtlasDecoder.cpp
        src/Level/Terrain/TextureAtlasDecoder.h
        src/Level/Terrain/EhfPalette.cpp
        src/Level/Terrain/EhfPalette.h
        src/Level/Rendering/ShaderBankFile.cpp
        src/Level/Rendering/ShaderBankFile.h
        src/Level/Terrain/TerrainTextureRegistry.cpp
        src/Level/Terrain/TerrainTextureRegistry.h
        src/Level/Terrain/EhfChunkParser.cpp
        src/Level/Terrain/EhfChunkParser.h
        src/Level/Terrain/EhfLodThumbnails.cpp
        src/Level/Terrain/EhfLodThumbnails.h
        src/Level/Terrain/TerrainSplat.cpp
        src/Level/Terrain/TerrainSplat.h
        src/Level/Terrain/TerrainEdit.cpp
        src/Level/Terrain/TerrainEdit.h
        src/Level/Editing/LevelEdit.cpp
        src/Level/Editing/LevelEdit.h
        src/Level/IO/BnkWriter.cpp
        src/Level/IO/BnkWriter.h
        src/UI/LevelGizmo.cpp
        src/UI/LevelGizmo.h
        src/Level/IO/VfsConfig.cpp
        src/Level/IO/VfsConfig.h
        src/GDB/GdbModelHashlist.cpp
        src/GDB/GdbModelHashlist.h
        src/GDB/GdbEdit.cpp
        src/GDB/GdbEdit.h
        src/GDB/GdbParser.cpp
        src/GDB/GdbParser.h
        src/GDB/GdbReaderInternal.h
        src/Level/Skybox/CloudBackgroundGeneration.cpp
        src/Level/Skybox/CloudBackgroundGeneration.h
        src/Level/Skybox/CloudBackgroundMap.cpp
        src/Level/Skybox/CloudBackgroundMap.h
        src/Level/Skybox/CloudPpcRecipes.cpp
        src/Level/Skybox/CloudPpcRecipes.h
        src/Level/Skybox/CloudRuntime.cpp
        src/Level/Skybox/CloudRuntime.h
        src/Level/Skybox/CloudShaderBank.cpp
        src/Level/Skybox/CloudShaderBank.h
        src/Level/Skybox/CloudTextureBindings.cpp
        src/Level/Skybox/CloudTextureBindings.h
        src/Level/Skybox/CloudXenosSubmission.cpp
        src/Level/Skybox/CloudXenosSubmission.h
        src/Level/Skybox/EnvironmentTheme.h
        src/Level/Skybox/EnvironmentThemeParser.cpp
        src/Level/Skybox/EnvironmentThemeParser.h
        src/Level/Skybox/SkyboxPreviewBinding.cpp
        src/Level/Skybox/SkyboxPreviewBinding.h
        src/Level/Skybox/SkyboxRenderer.cpp
        src/Level/Skybox/SkyboxRenderer.h
        src/Level/Skybox/SkyboxTypes.h
        src/Level/Skybox/SkyDomeXex.cpp
        src/Level/Skybox/SkyDomeXex.h
        src/Level/Skybox/SkyXexDecomp.cpp
        src/Level/Skybox/SkyXexDecomp.h
        src/Level/Skybox/XenosShaderBinary.cpp
        src/Level/Skybox/XenosShaderBinary.h
        src/Level/Environment/WaterParser.cpp
        src/Level/Environment/WaterParser.h
        src/Level/Database/TextBank.cpp
        src/Level/Database/TextBank.h
        src/Level/Effects/ParticleBank.cpp
        src/Level/Effects/ParticleBank.h
        src/Level/Effects/ParticleFX.cpp
        src/Level/Effects/ParticleFX.h
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
        src/UI/EntityModelResolver.cpp
        src/UI/EntityModelResolver.h
        src/Lua.cpp
        src/Lua.h
        src/lua/lua_decompile.h
        src/lua/lua_parse.cpp
        src/lua/lua_decompile.cpp
        src/lua/lua_decompiler.cpp
        src/Quest/QuestGraph.cpp
        src/Quest/QuestGraph.h
        src/Quest/QuestAuthoring.cpp
        src/Quest/QuestAuthoring.h
        src/Quest/QuestInjection.cpp
        src/Quest/QuestInjection.h
        src/Quest/QuestReferences.cpp
        src/Quest/QuestReferences.h
        src/Quest/QuestRewards.cpp
        src/Quest/QuestRewards.h
        src/Quest/QuestStoryGraph.cpp
        src/Quest/QuestStoryGraph.h
        src/Quest/QuestWorldIndex.cpp
        src/Quest/QuestWorldIndex.h
        src/Entity/NpcAuthoring.cpp
        src/Entity/NpcAuthoring.h
        src/UI/Quest/QuestNodeView.cpp
        src/UI/Quest/QuestNodeView.h
        src/UI/ContentTabs.cpp
        src/UI/ContentTabs.h
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
        FABLE_ENABLE_RECONSTRUCTED_CLOUD_PREVIEW=$<BOOL:${FABLE_ENABLE_RECONSTRUCTED_CLOUD_PREVIEW}>
        FABLE_ENABLE_RECONSTRUCTED_SKY_PREVIEW=$<BOOL:${FABLE_ENABLE_RECONSTRUCTED_SKY_PREVIEW}>
)

include(CTest)
if(BUILD_TESTING)
    add_executable(cloud_runtime_tests
            tests/CloudRuntimeTests.cpp
            src/Level/Skybox/CloudRuntime.cpp
            src/Level/Skybox/CloudShaderBank.cpp
            src/Level/Skybox/XenosShaderBinary.cpp
    )
    target_include_directories(cloud_runtime_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME cloud_runtime_tests COMMAND cloud_runtime_tests)
    add_executable(cloud_ppc_recipe_tests
            tests/CloudPpcRecipesTests.cpp
            src/Level/Skybox/CloudPpcRecipes.cpp
    )
    target_include_directories(cloud_ppc_recipe_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME cloud_ppc_recipe_tests COMMAND cloud_ppc_recipe_tests)
    add_executable(cloud_background_map_tests
            tests/CloudBackgroundMapTests.cpp
            src/Level/Skybox/CloudBackgroundMap.cpp
    )
    target_include_directories(cloud_background_map_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME cloud_background_map_tests
             COMMAND cloud_background_map_tests)
    add_executable(cloud_background_generation_tests
            tests/CloudBackgroundGenerationTests.cpp
            src/Level/Skybox/CloudBackgroundGeneration.cpp
    )
    target_include_directories(cloud_background_generation_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME cloud_background_generation_tests
             COMMAND cloud_background_generation_tests)
    add_executable(cloud_texture_binding_tests
            tests/CloudTextureBindingsTests.cpp
            src/Level/Skybox/CloudTextureBindings.cpp
    )
    target_include_directories(cloud_texture_binding_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME cloud_texture_binding_tests
             COMMAND cloud_texture_binding_tests)
    add_executable(cloud_xenos_submission_tests
            tests/CloudXenosSubmissionTests.cpp
            src/Level/Skybox/CloudXenosSubmission.cpp
    )
    target_include_directories(cloud_xenos_submission_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME cloud_xenos_submission_tests
             COMMAND cloud_xenos_submission_tests)
    add_executable(xenos_shader_binary_tests
            tests/XenosShaderBinaryTests.cpp
            src/Level/Skybox/XenosShaderBinary.cpp
    )
    target_include_directories(xenos_shader_binary_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME xenos_shader_binary_tests
             COMMAND xenos_shader_binary_tests)
    if(WIN32 AND FABLE_ENABLE_RECONSTRUCTED_CLOUD_PREVIEW)
        add_executable(reconstructed_cloud_shader_compile_tests
                tests/CloudShaderCompileTests.cpp
        )
        target_link_libraries(reconstructed_cloud_shader_compile_tests PRIVATE
                d3dcompiler
        )
        add_test(NAME reconstructed_cloud_shader_compile_tests
                 COMMAND reconstructed_cloud_shader_compile_tests)
    endif()
endif()

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

if(BUILD_TESTING)
    add_executable(texture_atlas_decoder_tests
            tests/TextureAtlasDecoderTests.cpp
            src/Level/Terrain/TextureAtlasDecoder.cpp
    )
    target_include_directories(texture_atlas_decoder_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(texture_atlas_decoder_tests PRIVATE ZLIB::ZLIB)
    add_test(NAME texture_atlas_decoder_tests
             COMMAND texture_atlas_decoder_tests)

    add_executable(shader_bank_tests
            tests/ShaderBankTests.cpp
            src/Level/Rendering/ShaderBankFile.cpp
    )
    target_include_directories(shader_bank_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(shader_bank_tests PRIVATE ZLIB::ZLIB)
    add_test(NAME shader_bank_tests COMMAND shader_bank_tests)

    add_executable(gdb_edit_tests
            tests/GdbEditTests.cpp
            src/GDB/GdbEdit.cpp
    )
    target_include_directories(gdb_edit_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME gdb_edit_tests COMMAND gdb_edit_tests)

    add_executable(npc_authoring_tests
            tests/NpcAuthoringTests.cpp
            src/Entity/NpcAuthoring.cpp
            src/GDB/GdbEdit.cpp
    )
    target_include_directories(npc_authoring_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

    add_executable(quest_graph_tests
            src/Quest/tests/QuestGraphTests.cpp
            src/Quest/QuestAuthoring.cpp
            src/Quest/QuestGraph.cpp
            src/Quest/QuestStoryGraph.cpp
    )
    target_include_directories(quest_graph_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    add_test(NAME quest_graph_tests COMMAND quest_graph_tests)

    add_executable(gdb_contents_probe
            tests/GdbContentsProbe.cpp
            src/GDB/GdbParser.cpp
            src/Level/Skybox/EnvironmentThemeParser.cpp
    )
    target_include_directories(gdb_contents_probe PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )

endif()

add_executable(shader_bank_extract
        tools/ShaderBankExtract.cpp
        src/Level/Rendering/ShaderBankFile.cpp
        src/Level/Skybox/CloudShaderBank.cpp
        src/Level/Skybox/XenosShaderBinary.cpp
)
target_include_directories(shader_bank_extract PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(shader_bank_extract PRIVATE ZLIB::ZLIB)

add_executable(quest_script_probe
        tools/quests/QuestScriptProbe.cpp
        src/ISO/IsoMount.cpp
        src/Level/Database/TextBank.cpp
        src/Quest/QuestGraph.cpp
        src/Quest/QuestReferences.cpp
        src/Quest/QuestStoryGraph.cpp
        src/lua/lua_parse.cpp
        src/lua/lua_decompile.cpp
        src/lua/lua_decompiler.cpp
)
target_include_directories(quest_script_probe PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(quest_script_probe PRIVATE ZLIB::ZLIB)

include(FetchContent)
FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG v1.91.5
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
        imgui_node_editor
        GIT_REPOSITORY https://github.com/thedmd/imgui-node-editor.git
        # develop commit with Dear ImGui 1.90+ compatibility (v0.9.3 still
        # uses removed key APIs and duplicates ImVec2 equality operators).
        GIT_TAG b302971455b3719ec9b5fb94b2f92d27c62b9ff0
)
FetchContent_MakeAvailable(imgui_node_editor)

target_sources(Fable_2_Asset_Browser PRIVATE
        ${imgui_node_editor_SOURCE_DIR}/crude_json.cpp
        ${imgui_node_editor_SOURCE_DIR}/imgui_canvas.cpp
        ${imgui_node_editor_SOURCE_DIR}/imgui_node_editor.cpp
        ${imgui_node_editor_SOURCE_DIR}/imgui_node_editor_api.cpp
)
target_include_directories(Fable_2_Asset_Browser PRIVATE
        ${imgui_node_editor_SOURCE_DIR}
)

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

if(BUILD_TESTING)
    add_executable(bnk_writer_tests
            tests/BnkWriterTests.cpp
            src/Level/IO/BnkWriter.cpp
            src/ISO/IsoMount.cpp
    )
    target_include_directories(bnk_writer_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${imgui_SOURCE_DIR}
            ${imgui_hex_editor_SOURCE_DIR}
            ${directxmath_SOURCE_DIR}/Inc
    )
    if(WIN32)
        target_compile_definitions(bnk_writer_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    endif()
    target_link_libraries(bnk_writer_tests PRIVATE ZLIB::ZLIB)
    add_test(NAME bnk_writer_tests COMMAND bnk_writer_tests)
endif()
