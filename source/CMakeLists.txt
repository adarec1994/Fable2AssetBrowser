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
option(FABLE_BUILD_TESTS
       "Build the project test executables"
       OFF)
option(FABLE_BUILD_TOOLS
       "Build optional command-line tools"
       OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

set(APP_SOURCES
        src/main.cpp
        src/Utilities/CrashLog.cpp
        src/Utilities/CrashLog.h
        src/Utilities/GameBackup.cpp
        src/Utilities/GameBackup.h
        src/Utilities/State.cpp
        src/Utilities/Utils.cpp
        src/Utilities/Files.cpp
        src/Utilities/AutoPilot.cpp
        src/textures/TexParser.cpp
        src/textures/LhTexCodec.cpp
        src/animations/AnimBank.cpp
        src/animations/AnimBank/State/Reader.inl
        src/animations/AnimBank/Toc/Parse.inl
        src/animations/AnimBank/Toc/Load.inl
        src/animations/AnimBank/Clips/Duration.inl
        src/animations/AnimBank/Gdb/BasicHelpers.inl
        src/animations/AnimBank/Gdb/View.inl
        src/animations/AnimBank/Gdb/StatsAndNames.inl
        src/animations/AnimBank/Gdb/Traversal.inl
        src/animations/AnimBank/Gdb/ClipRefs.inl
        src/animations/AnimBank/Gdb/Bindings.inl
        src/animations/AnimBank/Gdb/Scanning.inl
        src/animations/AnimBank/Resolve/Lua.inl
        src/animations/AnimBank/Resolve/Gdb.inl
        src/animations/AnimBank/Cache/Internals.inl
        src/animations/AnimBank/Cache/Load.inl
        src/animations/AnimBank/Cache/Save.inl
        src/animations/AnimBank/Bindings/Public.inl
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
        src/MDL/ModelParser/Cloth/Blocks.inl
        src/MDL/ModelParser/Cloth/Geometry.inl
        src/MDL/ModelParser/Core/BinaryUtilities.inl
        src/MDL/ModelParser/Engine/Geometry.inl
        src/MDL/ModelParser/Engine/Records.inl
        src/MDL/ModelParser/Geometry/Main.inl
        src/MDL/ModelParser/Loading/BufferAssembly.inl
        src/MDL/ModelParser/Parsing/Info/Foliage.inl
        src/MDL/ModelParser/Parsing/Info/HeaderAndMaterials.inl
        src/MDL/ModelParser/Parsing/Info/StandardBuffers.inl
        src/MDL/ModelParser/Reparse/Foliage.inl
        src/MDL/ModelParser/Reparse/MissingBuffers.inl
        src/MDL/ModelParser/Reparse/MultiInstance.inl
        src/MDL/ModelParser/Reparse/Polymsh.inl
        src/MDL/MdlFbxExport.cpp
        src/MDL/MdlFbxExport/Encoding/BufferAndProperties.inl
        src/MDL/MdlFbxExport/Encoding/Nodes.inl
        src/MDL/MdlFbxExport/Math/Types.inl
        src/MDL/MdlFbxExport/Math/Transforms.inl
        src/MDL/MdlFbxExport/Export/Setup.inl
        src/MDL/MdlFbxExport/Export/Materials.inl
        src/MDL/MdlFbxExport/Export/Animations.inl
        src/MDL/MdlFbxExport/Export/Document.inl
        src/MDL/MdlFbxExport/Export/Objects/Bones.inl
        src/MDL/MdlFbxExport/Export/Objects/Meshes.inl
        src/MDL/MdlFbxExport/Export/Objects/Animation.inl
        src/MDL/MdlFbxExport/Export/Connections.inl
        src/MDL/MdlFbxExport/Export/Write.inl
        src/MDL/MdlTexExport.cpp
        src/Import/ImageLoad.cpp
        src/Import/ImageLoad.h
        src/Import/TexWriter.cpp
        src/Import/TexWriter.h
        src/Import/GlbImport.cpp
        src/Import/GlbImport.h
        src/Import/MdlWriter.cpp
        src/Import/MdlWriter.h
        src/Import/AssetImport.cpp
        src/Import/AssetImport/Bank/InjectionAndValidation.inl
        src/Import/AssetImport/Textures/Targets.inl
        src/Import/AssetImport/Textures/Resolve.inl
        src/Import/AssetImport/Textures/Replace.inl
        src/Import/AssetImport/Models/SpawnTemplate.inl
        src/Import/AssetImport/Naming/Sanitize.inl
        src/Import/AssetImport/Models/ImportGlb.inl
        src/Import/AssetImport/Textures/ImportImage.inl
        src/Import/AssetImport/Textures/ReplaceSelected.inl
        src/Import/AssetImport/Folders/Import.inl
        src/Import/AssetImport.h
        src/Import/Xbox360Tiling.h
        src/UI/Panels/ImportDialog.cpp
        src/UI/Panels/ImportDialog.h
        src/Utilities/Progress.cpp
        src/Utilities/operations.cpp
        src/Utilities/operations/Paths/Output.inl
        src/Utilities/operations/Extraction/SelectedRaw.inl
        src/Utilities/operations/Audio/SelectedWav.inl
        src/Utilities/operations/Extraction/DumpAll.inl
        src/Utilities/operations/Audio/Export.inl
        src/Utilities/operations/Textures/RebuildAll.inl
        src/Utilities/operations/Models/RebuildAll.inl
        src/Utilities/operations/Textures/RebuildOne.inl
        src/Utilities/operations/Models/RebuildOne.inl
        src/Utilities/operations/Global/DumpAll.inl
        src/Utilities/operations/Global/Audio.inl
        src/Utilities/operations/Global/Textures.inl
        src/Utilities/operations/Global/Models.inl
        src/Utilities/operations/Animation/Selected.inl
        src/Utilities/operations/Animation/All.inl
        src/Utilities/operations/Glb/Selected.inl
        src/Utilities/operations/Glb/All.inl
        src/Utilities/operations/Glb/Global.inl
        src/UI/HexView.cpp
        src/UI/OutputLog.cpp
        src/UI/IconButton.cpp
        src/UI/UI_Main.cpp
        src/UI/Panels/Selection.cpp
        src/UI/Panels/Selection/Audio/Player.inl
        src/UI/Panels/Selection/Models/Helpers.inl
        src/UI/Panels/Selection/Gdb/Cache.inl
        src/UI/Panels/Selection/Banks/Pick.inl
        src/UI/Panels/Selection/Sources/Iso.inl
        src/UI/Panels/Selection/Sources/Folder.inl
        src/UI/Panels/Selection/Models/ReconstructNested.inl
        src/UI/Panels/Selection/Audio/Paths.inl
        src/UI/Panels/Selection/Banks/Extract.inl
        src/UI/Panels/Selection/Models/ReconstructPaired.inl
        src/UI/Panels/Selection/Export/Helpers.inl
        src/UI/Panels/Selection/Export/RawAudio.inl
        src/UI/Panels/Selection/Export/EncodedAudio.inl
        src/UI/Panels/Selection/Export/Asset.inl
        src/UI/Panels/Selection/Context/FileMenu.inl
        src/UI/Panels/TreeBuilder.cpp
        src/UI/Panels/TreeRender.cpp
        src/UI/Panels/LeftPanel.cpp
        src/UI/Panels/LeftPanel/Assets/Selection.inl
        src/UI/Panels/LeftPanel/Draw/Assets/FlatAssetList.inl
        src/UI/Panels/LeftPanel/Draw/Layout/Tabs.inl
        src/UI/Panels/LeftPanel/Draw/Tabs/Animations.inl
        src/UI/Panels/LeftPanel/Draw/Tabs/AssetLists.inl
        src/UI/Panels/LeftPanel/Draw/Tabs/Banks.inl
        src/UI/Panels/LeftPanel/Draw/Tabs/Entities.inl
        src/UI/Panels/LeftPanel/Draw/Tabs/FileTree.inl
        src/UI/Panels/LeftPanel/Draw/Tabs/Items.inl
        src/UI/Panels/LeftPanel/Draw/Tabs/Levels.inl
        src/UI/Panels/LeftPanel/Draw/Tabs/Quests.inl
        src/UI/Panels/LeftPanel/Draw/Tabs/ScriptsAndDetails.inl
        src/UI/Panels/LeftPanel/Drill/Navigation.inl
        src/UI/Panels/LeftPanel/Drill/State.inl
        src/UI/Panels/LeftPanel/Drill/TreeOpening.inl
        src/UI/Panels/LeftPanel/EntityAuthoring/Fields.inl
        src/UI/Panels/LeftPanel/EntityAuthoring/Modal.inl
        src/UI/Panels/LeftPanel/EntityAuthoring/Pickers.inl
        src/UI/Panels/LeftPanel/Layout/Dimensions.inl
        src/UI/Panels/LeftPanel/Quests/Injection.inl
        src/UI/Panels/LeftPanel/Quests/Search.inl
        src/UI/Panels/LeftPanel/Quests/Selection.inl
        src/UI/Panels/LeftPanel/Scripts/Selection.inl
        src/UI/Panels/LeftPanel/State/EntityCreation.inl
        src/UI/Panels/FileTable.cpp
        src/UI/Panels/FolderDialog.cpp
        src/UI/Panels/RightPanel.cpp
        src/UI/Panels/RightPanel/Actions/Primary.inl
        src/UI/Panels/RightPanel/Actions/HexAndSelection.inl
        src/UI/Panels/RightPanel/Preview/Load.inl
        src/UI/Panels/RightPanel/Export/Glb.inl
        src/UI/Panels/RightPanel/Filters/Extension.inl
        src/UI/Panels/RightPanel/Search/Global.inl
        src/UI/Panels/RightPanel/Content/LuaAndTable.inl
        src/UI/Panels/RightPanel/Pending/Model.inl
        src/UI/Panels/RightPanel/Pending/Texture.inl
        src/UI/Panels/RightPanel/Pending/Build.inl
        src/UI/Panels/RightPanel/Finalize.inl
        src/UI/Panels/PendingLoads.cpp
        src/UI/Panels/PendingLoads/Cache/State.inl
        src/UI/Panels/PendingLoads/Editing/ContainerSpawning.inl
        src/UI/Panels/PendingLoads/Editing/EntitySpawning.inl
        src/UI/Panels/PendingLoads/Editing/Guard.inl
        src/UI/Panels/PendingLoads/Editing/ModelSpawning.inl
        src/UI/Panels/PendingLoads/Geometry/Batching.inl
        src/UI/Panels/PendingLoads/Geometry/PropAppending.inl
        src/UI/Panels/PendingLoads/Geometry/Transforms.inl
        src/UI/Panels/PendingLoads/Models/Loading.inl
        src/UI/Panels/PendingLoads/Models/Paths.inl
        src/UI/Panels/PendingLoads/Processing/DirectX/HeightmapViews.inl
        src/UI/Panels/PendingLoads/Processing/DirectX/Terrain.inl
        src/UI/Panels/PendingLoads/Processing/Guards.inl
        src/UI/Panels/PendingLoads/Processing/ModelLoads.inl
        src/UI/Panels/PendingLoads/Processing/OpenGL/Terrain.inl
        src/UI/Panels/PendingLoads/Processing/PreviewBuild.inl
        src/UI/Panels/PendingLoads/Processing/TextureLoads.inl
        src/UI/Panels/PendingLoads/Streaming/Start.inl
        src/UI/Panels/PendingLoads/Streaming/State.inl
        src/UI/Panels/PendingLoads/Streaming/Upload.inl
        src/UI/Panels/PendingLoads/Streaming/Worker.inl
        src/UI/Panels/PendingLoads/Terrain/GeneratedTextures.inl
        src/UI/Panels/NewLevelDialog.cpp
        src/UI/Panels/LandscapePanel.cpp
        src/UI/Panels/DetailsPanel.cpp
        src/UI/Panels/NewLevelDialog.h
        src/UI/AudioPlayerWindow.cpp
        src/UI/Layout/LoadingScreen.cpp
        src/UI/Layout/MainLayout.cpp
        src/UI/Layout/RenderPanel.cpp
        src/UI/Layout/RenderPanel/Shared/ContentViews.inl
        src/UI/Layout/RenderPanel/DirectX/EntityDetails.inl
        src/UI/Layout/RenderPanel/DirectX/EntityDetails/Overlays/GdbPlacements.inl
        src/UI/Layout/RenderPanel/DirectX/EntityDetails/State/Selection.inl
        src/UI/Layout/RenderPanel/DirectX/EntityDetails/Containers/Labels.inl
        src/UI/Layout/RenderPanel/DirectX/EntityDetails/Containers/AuthoredRules.inl
        src/UI/Layout/RenderPanel/DirectX/EntityDetails/Containers/LootEditor.inl
        src/UI/Layout/RenderPanel/DirectX/EntityDetails/Details/EntityGameplay.inl
        src/UI/Layout/RenderPanel/DirectX/EntityDetails/Details/Property.inl
        src/UI/Layout/RenderPanel/DirectX/EntityDetails/Details/ExistingContainer.inl
        src/UI/Layout/RenderPanel/DirectX/EntityDetails/Details/AdditionContainer.inl
        src/UI/Layout/RenderPanel/DirectX/EntityDetails/Containers/SpawnChoices.inl
        src/UI/Layout/RenderPanel/DirectX/EntityDetails/Quest/LevelReference.inl
        src/UI/Layout/RenderPanel/DirectX/EntityDetails/Selection/SpawnMarker.inl
        src/UI/Layout/RenderPanel/DirectX/EntityDetails/Quest/AddItem.inl
        src/UI/Layout/RenderPanel/DirectX/EntityDetails/Overlays/SpawnMarkers.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Scene/Setup.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Placement/DragDropAndStarts.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Placement/AddMenu.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Interaction/SkeletonCameraAndRender.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Editing/Markers.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Editing/Selection/Setup.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Editing/Selection/Metadata.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Editing/Selection/TransformOverlay.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Editing/Selection/Inventory.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Editing/Selection/GizmoAndDelete.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Controls/ViewportOverlays.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Materials/MaterialsAndLods.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Editing/Terrain.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Animation/Controls.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Details/Item.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Details/Entity.inl
        src/UI/Layout/RenderPanel/DirectX/ModelView/Popout/Texture.inl
        src/UI/Layout/RenderPanel/DirectX/Popouts.inl
        src/UI/Layout/RenderPanel/DirectX/PublicApi.inl
        src/UI/Layout/RenderPanel/DirectX/SceneInteraction.inl
        src/UI/Layout/RenderPanel/DirectX/SceneProjection.inl
        src/UI/Layout/RenderPanel/DirectX/TexturePreview.inl
        src/UI/Layout/RenderPanel/OpenGL/ModelView.inl
        src/UI/Layout/RenderPanel/OpenGL/Overlays.inl
        src/UI/Layout/RenderPanel/OpenGL/Popouts.inl
        src/UI/Layout/RenderPanel/OpenGL/PublicApi.inl
        src/UI/Layout/RenderPanel/OpenGL/TexturePreview.inl
        src/UI/About/AboutWindow.cpp
        src/Splashscreen/Splashscreen.cpp
        src/Splashscreen/SplashSparkles.cpp
        src/Splashscreen/LogoMap.cpp
        src/Splashscreen/IconFont.cpp
        src/ISO/IsoExtract.cpp
        src/ISO/IsoMount.cpp
        src/ISO/IsoDump.cpp
        src/ISO/IsoDump/Internal/IsoCopy.inl
        src/ISO/IsoDump/Dump/IsoContents.inl
        src/ISO/IsoDump/Common/OutputAndBnk.inl
        src/ISO/IsoDump/Models/Reconstruct.inl
        src/ISO/IsoDump/Models/DumpRaw.inl
        src/ISO/IsoDump/Models/ExportHelpers.inl
        src/ISO/IsoDump/Models/DumpConverted.inl
        src/ISO/IsoDump/Textures/Reconstruct.inl
        src/ISO/IsoDump/Textures/DumpRaw.inl
        src/ISO/IsoDump/Textures/DumpConverted.inl
        src/ISO/IsoDump/Audio/DumpRaw.inl
        src/ISO/IsoDump/Audio/DumpConverted.inl
        src/ISO/IsoDump/Bnk/DumpContents.inl
        src/ISO/IsoDump/Gdb/DumpSave.inl
        src/MDL/mdl_converter.cpp
        src/Level/Core/LevelLoader.cpp
        src/Level/Core/LevelLoader/State/Globals.inl
        src/Level/Core/LevelLoader/Platform/LinuxPreview.inl
        src/Level/Core/LevelLoader/Models/GlobalLookup.inl
        src/Level/Core/LevelLoader/Async/Loading.inl
        src/Level/Core/LevelLoader/Open/Progress.inl
        src/Level/Core/LevelLoader/Open/Parsing.inl
        src/Level/Core/LevelLoader/Open/Resources/Heightfields.inl
        src/Level/Core/LevelLoader/Open/Resources/Vfs.inl
        src/Level/Core/LevelLoader/Open/Content.inl
        src/Level/Core/LevelLoader/Open/Collision.inl
        src/Level/Core/LevelLoader/Open/Terrain/Main.inl
        src/Level/Core/LevelLoader/Open/Terrain/Adjacent/Helpers.inl
        src/Level/Core/LevelLoader/Open/Terrain/Adjacent/Loading.inl
        src/Level/Core/LevelLoader/Open/Terrain/PendingState.inl
        src/Level/Core/LevelLoader/Open/Terrain/Props.inl
        src/Level/Core/LevelLoader/Open/Terrain/PlacementValidation.inl
        src/Level/Core/LevelLoader/Open/Terrain/Water.inl
        src/Level/Core/LevelLoader/Open/Terrain/Handoff.inl
        src/Level/Core/LevelLoader.h
        src/Level/Loading/LevelBinaryReader.h
        src/Level/Loading/EngineLevelParser.cpp
        src/Level/Loading/LevelCatalogLoader.cpp
        src/Level/Loading/LevelCatalogLoaderInternal.h
        src/Level/Loading/LevelTerrainLoader.cpp
        src/Level/Loading/LevelTerrainLoader/Preview/Heightmap.inl
        src/Level/Loading/LevelTerrainLoader/Preview/Pf99.inl
        src/Level/Loading/LevelTerrainLoader/Atlas/Decode.inl
        src/Level/Loading/LevelTerrainLoader/Tiles/DescriptorsAndParsing.inl
        src/Level/Loading/LevelTerrainLoader/Tiles/EmbeddedBc1.inl
        src/Level/Loading/LevelTerrainLoader/Tiles/Compositing.inl
        src/Level/Loading/LevelTerrainLoader/Vista/PageDecode.inl
        src/Level/Loading/LevelTerrainLoader/Vista/PatchGeometry.inl
        src/Level/Loading/LevelTerrainLoader/Vista/Public.inl
        src/Level/Loading/LevelTerrainLoader/Albedo/Decode.inl
        src/Level/Loading/LevelTerrainLoader/Albedo/PaletteFallback.inl
        src/Level/Loading/LevelTerrainLoader/Composite/Wrappers.inl
        src/Level/Loading/LevelTerrainLoader/Composite/Setup.inl
        src/Level/Loading/LevelTerrainLoader/Composite/Sampling.inl
        src/Level/Loading/LevelTerrainLoader/Composite/Paint.inl
        src/Level/Loading/LevelTerrainMesh.cpp
        src/Level/Loading/LevelTerrainLoaderInternal.h
        src/Level/Loading/Stages/LoadContainers.inl
        src/Level/Loading/Stages/LoadEffects.inl
        src/Level/Loading/Stages/LoadEntities.inl
        src/Level/Loading/Stages/LoadEnvironment.inl
        src/Level/Loading/Stages/LoadProps.inl
        src/Level/Loading/Stages/LoadProps/Finalize/Append.inl
        src/Level/Loading/Stages/LoadProps/Gdb/Diagnostics.inl
        src/Level/Loading/Stages/LoadProps/Gdb/GmdLayouts.inl
        src/Level/Loading/Stages/LoadProps/Gdb/MarketShells.inl
        src/Level/Loading/Stages/LoadProps/Gdb/NoHashShells.inl
        src/Level/Loading/Stages/LoadProps/Gdb/Placements.inl
        src/Level/Loading/Stages/LoadProps/Gdb/State.inl
        src/Level/Loading/Stages/LoadProps/Havok/Fallback.inl
        src/Level/Loading/Stages/LoadProps/Placements/Initial.inl
        src/Level/Loading/Stages/LoadProps/Resolution/ModelIndex.inl
        src/Level/Loading/Stages/LevelPropSupport.inl
        src/Level/Loading/Stages/LevelPropSupport/Models/Sockets.inl
        src/Level/Loading/Stages/LevelPropSupport/Models/Types.inl
        src/Level/Loading/Stages/LevelPropSupport/Transforms/Math.inl
        src/Level/Loading/Stages/LevelPropSupport/Diagnostics/Bridge.inl
        src/Level/Loading/Stages/LevelPropSupport/Models/NamesAndPayload.inl
        src/Level/Loading/Stages/LevelPropSupport/Filtering/Shells.inl
        src/Level/Loading/Stages/LevelPropSupport/Keys/Identification.inl
        src/Level/Loading/Stages/LevelPropSupport/Streaming/Resolution.inl
        src/Level/Loading/Stages/LevelPropSupport/Streaming/Scoring.inl
        src/Level/Core/LevelExport.cpp
        src/Level/Core/LevelExport/State/Helpers.inl
        src/Level/Core/LevelExport/IO/Files.inl
        src/Level/Core/LevelExport/Terrain/Glb.inl
        src/Level/Core/LevelExport/Terrain/Fbx/Encoding.inl
        src/Level/Core/LevelExport/Terrain/Fbx/Write.inl
        src/Level/Core/LevelExport/Terrain/Weights.inl
        src/Level/Core/LevelExport/Scene/Instances.inl
        src/Level/Core/LevelExport/Scene/Json.inl
        src/Level/Core/LevelExport/Run/SetupAndModels.inl
        src/Level/Core/LevelExport/Run/Terrain.inl
        src/Level/Core/LevelExport/Run/ManifestModels.inl
        src/Level/Core/LevelExport/Run/ManifestTerrain.inl
        src/Level/Core/LevelExport/Run/Finish.inl
        src/Level/Core/LevelExport/PublicApi.inl
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
        src/Level/Editing/LevelEdit/Assets/DependencyLookup.inl
        src/Level/Editing/LevelEdit/Diagnostics/LevProbe.inl
        src/Level/Editing/LevelEdit/Editing/Containers.inl
        src/Level/Editing/LevelEdit/Editing/Lifecycle.inl
        src/Level/Editing/LevelEdit/Editing/Spawns.inl
        src/Level/Editing/LevelEdit/Editing/Transforms.inl
        src/Level/Editing/LevelEdit/Internal/Compression.inl
        src/Level/Editing/LevelEdit/Internal/State.inl
        src/Level/Editing/LevelEdit/Internal/Transforms.inl
        src/Level/Editing/LevelEdit/Persistence/Sidecars.inl
        src/Level/Editing/LevelEdit/Saving/ContainerEdits.inl
        src/Level/Editing/LevelEdit/Saving/EntityCreation.inl
        src/Level/Editing/LevelEdit/Saving/EntityRemoval.inl
        src/Level/Editing/LevelEdit/Saving/Generators.inl
        src/Level/Editing/LevelEdit/Saving/Save.inl
        src/Level/Editing/LevelEdit/Saving/Save/Setup.inl
        src/Level/Editing/LevelEdit/Saving/Save/CollectEdits.inl
        src/Level/Editing/LevelEdit/Saving/Save/Patching/LevelAndGdb.inl
        src/Level/Editing/LevelEdit/Saving/Save/Entities/Rewrite.inl
        src/Level/Editing/LevelEdit/Saving/Save/Entities/Physics.inl
        src/Level/Editing/LevelEdit/Saving/Save/Bake/Setup.inl
        src/Level/Editing/LevelEdit/Saving/Save/Bake/LevelData.inl
        src/Level/Editing/LevelEdit/Saving/Save/Bake/Assets.inl
        src/Level/Editing/LevelEdit/Saving/Save/Bake/Streaming.inl
        src/Level/Editing/LevelEdit/Saving/Save/Finalize/DeferredState.inl
        src/Level/Editing/LevelEdit/Saving/Save/Rebuild/Level.inl
        src/Level/Editing/LevelEdit/Saving/Save/Rebuild/Entities.inl
        src/Level/Editing/LevelEdit/Saving/Save/Rebuild/Text.inl
        src/Level/Editing/LevelEdit/Saving/Save/Finalize/Commit.inl
        src/Level/Editing/LevelEdit/Saving/SaveRouting.inl
        src/Level/Editing/LevelEdit/Saving/SpawnPoints.inl
        src/Level/Editing/LevelEdit/Saving/WorkingCopy.inl
        src/Level/Editing/LevelEdit/Serialization/LevelPlacements.inl
        src/Level/Creation/NewLevel.cpp
        src/Level/Creation/LandscapeAuthoring.cpp
        src/Level/Creation/TiffHeightmap.cpp
        src/Level/Creation/SkyAuthoring.cpp
        src/Level/Creation/WaterAuthoring.cpp
        src/Level/Terrain/TerrainPaint.cpp
        src/Level/Creation/NewLevel.h
        src/Level/Creation/GameRegistry.cpp
        src/Level/Creation/GameRegistry.h
        src/Quest/Blueprint/BlueprintTypes.h
        src/Quest/Blueprint/BlueprintGraph.cpp
        src/Quest/Blueprint/BlueprintGraph.h
        src/Quest/Blueprint/BlueprintNodeRegistry.cpp
        src/Quest/Blueprint/BlueprintNodeRegistry.h
        src/Quest/Blueprint/BlueprintNodes_Events.cpp
        src/Quest/Blueprint/BlueprintNodes_Flow.cpp
        src/Quest/Blueprint/BlueprintNodes_Data.cpp
        src/Quest/Blueprint/BlueprintNodes_Quest.cpp
        src/Quest/Blueprint/BlueprintNodes_Dialogue.cpp
        src/Quest/Blueprint/BlueprintNodes_Inventory.cpp
        src/Quest/Blueprint/BlueprintNodes_Entity.cpp
        src/Quest/Blueprint/BlueprintNodes_World.cpp
        src/Quest/Blueprint/BlueprintNodes_Util.cpp
        src/Quest/Blueprint/BlueprintNodes_Prereq.cpp
        src/Quest/Blueprint/BlueprintNodes_GameVars.cpp
        src/Quest/Blueprint/BlueprintCompiler.cpp
        src/Quest/Blueprint/BlueprintCompiler.h
        src/Quest/Blueprint/BlueprintSerialize.cpp
        src/Quest/Blueprint/BlueprintSerialize.h
        src/UI/Quest/Blueprint/BlueprintPinWidgets.cpp
        src/UI/Quest/Blueprint/BlueprintPinWidgets.h
        src/UI/Quest/Blueprint/BlueprintEditor.cpp
        src/UI/Quest/Blueprint/BlueprintEditor.h
        src/UI/Quest/Blueprint/BlueprintCanvas.cpp
        src/UI/Quest/Blueprint/BlueprintCanvas.h
        src/UI/Quest/Blueprint/BlueprintNodeDrawer.cpp
        src/UI/Quest/Blueprint/BlueprintNodeDrawer.h
        src/UI/Quest/Blueprint/BlueprintPalette.cpp
        src/UI/Quest/Blueprint/BlueprintPalette.h
        src/UI/Quest/Blueprint/BlueprintInspector.cpp
        src/UI/Quest/Blueprint/BlueprintInspector.h
        src/UI/Quest/Blueprint/BlueprintPrerequisites.cpp
        src/UI/Quest/Blueprint/BlueprintPrerequisites.h
        src/UI/Quest/Blueprint/vendor/drawing.cpp
        src/UI/Quest/Blueprint/vendor/drawing.h
        src/UI/Quest/Blueprint/vendor/widgets.cpp
        src/UI/Quest/Blueprint/vendor/widgets.h
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
        src/GDB/GdbParser/Core/Definitions.inl
        src/GDB/GdbParser/Core/EmbeddedDictionary.inl
        src/GDB/GdbParser/Core/Names.inl
        src/GDB/GdbParser/Entities/Contents.inl
        src/GDB/GdbParser/Entities/CreatureCatalog.inl
        src/GDB/GdbParser/Entities/Gameplay.inl
        src/GDB/GdbParser/Entities/GameplayOptions.inl
        src/GDB/GdbParser/Entities/ModelResolution.inl
        src/GDB/GdbParser/Entities/MultiGdb.inl
        src/GDB/GdbParser/Entities/Properties.inl
        src/GDB/GdbParser/Entities/PropTemplates.inl
        src/GDB/GdbParser/Entities/Spawns.inl
        src/GDB/GdbParser/Entities/TextTags.inl
        src/GDB/GdbParser/Items/Catalog.inl
        src/GDB/GdbParser/Items/Details.inl
        src/GDB/GdbParser/Items/LabelFormatting.inl
        src/GDB/GdbParser/Models/FieldReaders.inl
        src/GDB/GdbParser/Models/ModelResolution.inl
        src/GDB/GdbParser/Particles/Attachments.inl
        src/GDB/GdbParser/Particles/Effects.inl
        src/GDB/GdbParser/Particles/NameLookup.inl
        src/GDB/GdbParser/Particles/Placements.inl
        src/GDB/GdbParser/Records/ModelLookup.inl
        src/GDB/GdbParser/Records/Parsing.inl
        src/GDB/GdbParser/Records/Rows.inl
        src/GDB/GdbParser/Transforms/InlineTransforms.inl
        src/GDB/GdbParser/Transforms/RecordTransforms.inl
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
        src/Level/Skybox/EnvironmentThemeParser/Schema/ConstantsAndTypes.inl
        src/Level/Skybox/EnvironmentThemeParser/Extractor/Public.inl
        src/Level/Skybox/EnvironmentThemeParser/Extractor/Lookup.inl
        src/Level/Skybox/EnvironmentThemeParser/Extractor/Apply/Water.inl
        src/Level/Skybox/EnvironmentThemeParser/Extractor/Apply/Sky.inl
        src/Level/Skybox/EnvironmentThemeParser/Extractor/Apply/Weather.inl
        src/Level/Skybox/EnvironmentThemeParser/Extractor/Apply/Cloud.inl
        src/Level/Skybox/EnvironmentThemeParser/Extractor/Selection.inl
        src/Level/Skybox/EnvironmentThemeParser/PublicApi.inl
        src/Level/Skybox/EnvironmentThemeParser.h
        src/Level/Skybox/SkyboxPreviewBinding.cpp
        src/Level/Skybox/SkyboxPreviewBinding.h
        src/Level/Skybox/SkyboxRenderer.cpp
        src/Level/Skybox/SkyboxRenderer/Diagnostics.inl
        src/Level/Skybox/SkyboxRenderer/Shaders/Sources.inl
        src/Level/Skybox/SkyboxRenderer/Shaders/Compile.inl
        src/Level/Skybox/SkyboxRenderer/Textures/Create.inl
        src/Level/Skybox/SkyboxRenderer/Textures/Release.inl
        src/Level/Skybox/SkyboxRenderer/Textures/PreferredBank.inl
        src/Level/Skybox/SkyboxRenderer/Frame/Camera.inl
        src/Level/Skybox/SkyboxRenderer/Frame/Evaluate.inl
        src/Level/Skybox/SkyboxRenderer/Resources/ClearColour.inl
        src/Level/Skybox/SkyboxRenderer/Resources/Create.inl
        src/Level/Skybox/SkyboxRenderer/Resources/Release.inl
        src/Level/Skybox/SkyboxRenderer/Resources/Reset.inl
        src/Level/Skybox/SkyboxRenderer/Rendering/Sky/Setup.inl
        src/Level/Skybox/SkyboxRenderer/Rendering/Sky/ExactDome.inl
        src/Level/Skybox/SkyboxRenderer/Rendering/Sky/Fallback.inl
        src/Level/Skybox/SkyboxRenderer/Rendering/Clouds.inl
        src/Level/Skybox/SkyboxRenderer.h
        src/Level/Skybox/SkyboxTypes.h
        src/Level/Skybox/SkyDomeXex.cpp
        src/Level/Skybox/SkyDomeXex.h
        src/Level/Skybox/SkyXexDecomp.cpp
        src/Level/Skybox/SkyXexDecomp.h
        src/Level/Skybox/XenosShaderBinary.cpp
        src/Level/Skybox/XenosShaderBinary/Metadata/Tables.inl
        src/Level/Skybox/XenosShaderBinary/Formatting/Destinations.inl
        src/Level/Skybox/XenosShaderBinary/Formatting/Sources.inl
        src/Level/Skybox/XenosShaderBinary/Formatting/Instructions.inl
        src/Level/Skybox/XenosShaderBinary/Decode/ControlFlowBits.inl
        src/Level/Skybox/XenosShaderBinary/Decode/AluCore.inl
        src/Level/Skybox/XenosShaderBinary/Decode/VertexFetch.inl
        src/Level/Skybox/XenosShaderBinary/Patching/VertexFetch.inl
        src/Level/Skybox/XenosShaderBinary/Decode/Alu.inl
        src/Level/Skybox/XenosShaderBinary/Decode/TextureFetch.inl
        src/Level/Skybox/XenosShaderBinary/Decode/Program.inl
        src/Level/Skybox/XenosShaderBinary/Metadata/OpcodeNames.inl
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
        src/Audio/Xma2Codec/XmaWmaProDecoder/State/TablesAndState.inl
        src/Audio/Xma2Codec/XmaWmaProDecoder/Math/Vector.inl
        src/Audio/Xma2Codec/XmaWmaProDecoder/Decode/Initialize.inl
        src/Audio/Xma2Codec/XmaWmaProDecoder/Decode/TilesAndTransforms.inl
        src/Audio/Xma2Codec/XmaWmaProDecoder/Decode/Coefficients.inl
        src/Audio/Xma2Codec/XmaWmaProDecoder/Decode/TransformsAndWindow.inl
        src/Audio/Xma2Codec/XmaWmaProDecoder/Decode/Subframe.inl
        src/Audio/Xma2Codec/XmaWmaProDecoder/Decode/Frame.inl
        src/Audio/Xma2Codec/XmaWmaProDecoder/Packet/Bits.inl
        src/Audio/Xma2Codec/XmaWmaProDecoder/Packet/Decode.inl
        src/Audio/Xma2Codec/XmaWmaProDecoder/Decoder/Impl.inl
        src/Audio/Xma2Codec/XmaWmaProDecoder/Decoder/Public.inl
        src/UI/ModelPreview.cpp
        src/UI/ModelPreview.h
        src/UI/ModelPreview/Camera/FlyCam.inl
        src/UI/ModelPreview/Common/Helpers.inl
        src/UI/ModelPreview/DirectX/Animation/Cloth.inl
        src/UI/ModelPreview/DirectX/Animation/Skeleton.inl
        src/UI/ModelPreview/DirectX/Animation/WorldPose.inl
        src/UI/ModelPreview/DirectX/Lifecycle.inl
        src/UI/ModelPreview/DirectX/Model/Build.inl
        src/UI/ModelPreview/DirectX/Particles.inl
        src/UI/ModelPreview/DirectX/Pipeline/Pipeline.inl
        src/UI/ModelPreview/DirectX/Rendering/Frame.inl
        src/UI/ModelPreview/DirectX/Resources/Release.inl
        src/UI/ModelPreview/DirectX/Resources/RenderTarget.inl
        src/UI/ModelPreview/DirectX/Resources/TextureCache.inl
        src/UI/ModelPreview/DirectX/Resources/Textures.inl
        src/UI/ModelPreview/DirectX/Shaders/Shaders.inl
        src/UI/ModelPreview/OpenGL/Lifecycle.inl
        src/UI/ModelPreview/OpenGL/Math/Matrices.inl
        src/UI/ModelPreview/OpenGL/Model/Build.inl
        src/UI/ModelPreview/OpenGL/Rendering/Frame.inl
        src/UI/ModelPreview/OpenGL/Resources.inl
        src/UI/ModelPreview/OpenGL/Textures.inl
        src/UI/ModelPreview/Textures/BlockCompression.inl
        src/UI/ModelPreview/Textures/Decoder.inl
        src/UI/ModelPreview/Textures/Lookup.inl
        src/UI/ModelPreview/Textures/XboxTiling.inl
        src/UI/EntityModelResolver.cpp
        src/UI/EntityModelResolver.h
        src/Lua.cpp
        src/lua/Bytecode/Definitions.inl
        src/lua/Bytecode/Reader.inl
        src/lua/Decompiler/StateAndHelpers.inl
        src/lua/Decompiler/Driver.inl
        src/lua/Decompiler/Instructions/Values.inl
        src/lua/Decompiler/Instructions/Flow.inl
        src/lua/Decompiler/Instructions/Calls.inl
        src/lua/Decompiler/Instructions/LoopsAndClosures.inl
        src/lua/API/Decompile.inl
        src/lua/API/Bridge.inl
        src/lua/Paths/Output.inl
        src/lua/Dump/Read.inl
        src/lua/Dump/Single.inl
        src/lua/Dump/All.inl
        src/Lua.h
        src/lua/lua_decompile.h
        src/lua/lua_parse.cpp
        src/lua/lua_decompile.cpp
        src/lua/lua_decompiler.cpp
        src/lua/Decompiler/Analysis/Variables.inl
        src/lua/Decompiler/ControlFlow/Blocks.inl
        src/lua/Decompiler/ControlFlow/Branches.inl
        src/lua/Decompiler/Core/Types.inl
        src/lua/Decompiler/Decompiler/Definition.inl
        src/lua/Decompiler/Expressions/Nodes.inl
        src/lua/Decompiler/Model/Runtime.inl
        src/lua/Decompiler/Output/Blocks.inl
        src/lua/Decompiler/Output/Expressions.inl
        src/lua/Decompiler/Processing/Branches.inl
        src/lua/Decompiler/Processing/Conditions.inl
        src/lua/Decompiler/Processing/Instructions.inl
        src/lua/Decompiler/Processing/Sequences.inl
        src/lua/Decompiler/Statements/Operations.inl
        src/Quest/QuestGraph.cpp
        src/Quest/QuestGraph/Core/TypesAndText.inl
        src/Quest/QuestGraph/Graph/ConstructionHelpers.inl
        src/Quest/QuestGraph/Narrative/Collection.inl
        src/Quest/QuestGraph/Narrative/Conditions.inl
        src/Quest/QuestGraph/Narrative/DescribeActions.inl
        src/Quest/QuestGraph/Narrative/DescribeStart.inl
        src/Quest/QuestGraph/Narrative/DescribeWorld.inl
        src/Quest/QuestGraph/Narrative/Fallback.inl
        src/Quest/QuestGraph/Parsing/FactsAndReferences.inl
        src/Quest/QuestGraph/Parsing/ValuesAndWorld.inl
        src/Quest/QuestGraph/Public/BuildGraph.inl
        src/Quest/QuestGraph/Public/Names.inl
        src/Quest/QuestGraph/Public/StringLiterals.inl
        src/Quest/QuestGraph.h
        src/Quest/QuestAuthoring.cpp
        src/Quest/QuestAuthoring/Lua/Helpers.inl
        src/Quest/QuestAuthoring/Flow/Validation.inl
        src/Quest/QuestAuthoring/Childhood/Validation.inl
        src/Quest/QuestAuthoring/Childhood/Generate.inl
        src/Quest/QuestAuthoring/Model/Quest.inl
        src/Quest/QuestAuthoring/Metadata/Names.inl
        src/Quest/QuestAuthoring/Metadata/Milestones.inl
        src/Quest/QuestAuthoring/Validation/Simple.inl
        src/Quest/QuestAuthoring/Text/Entries.inl
        src/Quest/QuestAuthoring/Generation/QuestLua.inl
        src/Quest/QuestAuthoring/Generation/Eligibility.inl
        src/Quest/QuestAuthoring/Generation/Preview.inl
        src/Quest/QuestAuthoring/Patching/Gameflow.inl
        src/Quest/QuestAuthoring.h
        src/Quest/QuestInjection.cpp
        src/Quest/QuestInjection.h
        src/Quest/QuestReferences.cpp
        src/Quest/QuestReferences.h
        src/Quest/QuestRewards.cpp
        src/Quest/QuestRewards.h
        src/Quest/QuestStoryGraph.cpp
        src/Quest/QuestStoryGraph/Core/TypesAndText.inl
        src/Quest/QuestStoryGraph/Layout/Branching.inl
        src/Quest/QuestStoryGraph/Parsing/Classification.inl
        src/Quest/QuestStoryGraph/Public/BuildStoryGraph.inl
        src/Quest/QuestStoryGraph/References/Metadata.inl
        src/Quest/QuestStoryGraph/Story/BeatConstruction.inl
        src/Quest/QuestStoryGraph/Story/ChildhoodHelpers.inl
        src/Quest/QuestStoryGraph/Story/NodeEmission.inl
        src/Quest/QuestStoryGraph/Story/RewardsAndKinds.inl
        src/Quest/QuestStoryGraph/Timelines/Childhood.inl
        src/Quest/QuestStoryGraph/Timelines/Frankenbride.inl
        src/Quest/QuestStoryGraph.h
        src/Quest/QuestWorldIndex.cpp
        src/Quest/QuestWorldIndex.h
        src/Entity/NpcAuthoring.cpp
        src/Entity/NpcAuthoring.h
        src/Entity/StaticPropAuthoring.cpp
        src/Entity/StaticPropAuthoring.h
        src/UI/Quest/QuestNodeView.cpp
        src/UI/Quest/QuestNodeView/State/StateAndCatalog.inl
        src/UI/Quest/QuestNodeView/Rendering/IdsColorsAndEditor.inl
        src/UI/Quest/QuestNodeView/Rendering/Pins.inl
        src/UI/Quest/QuestNodeView/Rendering/GraphNode.inl
        src/UI/Quest/QuestNodeView/Authoring/StoryMilestones.inl
        src/UI/Quest/QuestNodeView/Authoring/NodePresentation.inl
        src/UI/Quest/QuestNodeView/Pickers/Items.inl
        src/UI/Quest/QuestNodeView/References/Summary.inl
        src/UI/Quest/QuestNodeView/Authoring/QuestLifecycle.inl
        src/UI/Quest/QuestNodeView/Authoring/Prerequisites/Requests.inl
        src/UI/Quest/QuestNodeView/References/Binding.inl
        src/UI/Quest/QuestNodeView/Authoring/Prerequisites/Conditions.inl
        src/UI/Quest/QuestNodeView/Authoring/Npc/Creation.inl
        src/UI/Quest/QuestNodeView/Authoring/Npc/Instances.inl
        src/UI/Quest/QuestNodeView/ReadOnly/Helpers.inl
        src/UI/Quest/QuestNodeView/ReadOnly/Inspector.inl
        src/UI/Quest/QuestNodeView/References/Catalog.inl
        src/UI/Quest/QuestNodeView/Graph/Source.inl
        src/UI/Quest/QuestNodeView/Graph/ViewState.inl
        src/UI/Quest/QuestNodeView/Graph/Draw.inl
        src/UI/Quest/QuestNodeView/Graph/Shutdown.inl
        src/UI/Quest/QuestNodeView.h
        src/UI/ContentTabs.cpp
        src/UI/ContentTabs.h
)

# The parsing/terrain/texture hot paths dominate level-load time; compile
# them optimized even in Debug so the debug build stays usable. /RTC1 is
# stripped globally because it cannot coexist with per-file /O2.
if(MSVC)
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    set(F2AB_HOT_SOURCES
        src/BNKReader.cpp
        src/GDB/GdbParser.cpp
        src/Level/Core/LevelLoader.cpp
        src/Level/Core/LevelExport.cpp
        src/Level/Loading/LevelTerrainLoader.cpp
        src/Level/Loading/LevelCatalogLoader.cpp
        src/Level/Loading/EngineLevelParser.cpp
        src/Level/Terrain/HeightfieldLoader.cpp
        src/Level/Terrain/TextureAtlasDecoder.cpp
        src/Level/Terrain/EhfChunkParser.cpp
        src/Level/Terrain/TerrainEdit.cpp
        src/Level/Terrain/TerrainPaint.cpp
        src/Level/Environment/WaterParser.cpp
        src/Level/Skybox/EnvironmentThemeParser.cpp
        src/textures/TexParser.cpp
        src/textures/LhTexCodec.cpp
        src/MDL/ModelParser.cpp
        src/animations/AnimBank.cpp
    )
    set_source_files_properties(${F2AB_HOT_SOURCES} PROPERTIES
        COMPILE_OPTIONS "$<$<CONFIG:Debug>:/O2>")
endif()

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

if(FABLE_BUILD_TESTS)
    include(CTest)
endif()
if(FABLE_BUILD_TESTS)
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

if(FABLE_BUILD_TESTS)
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

    add_executable(static_prop_authoring_tests
            tests/StaticPropAuthoringTests.cpp
            src/Entity/StaticPropAuthoring.cpp
            src/GDB/GdbEdit.cpp
    )
    target_include_directories(static_prop_authoring_tests PRIVATE
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

if(FABLE_BUILD_TOOLS)
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
endif()

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

if(FABLE_BUILD_TESTS)
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

    add_executable(blueprint_tests
            src/Quest/Blueprint/tests/BlueprintTests.cpp
            src/Quest/Blueprint/BlueprintGraph.cpp
            src/Quest/Blueprint/BlueprintNodeRegistry.cpp
            src/Quest/Blueprint/BlueprintNodes_Events.cpp
            src/Quest/Blueprint/BlueprintNodes_Flow.cpp
            src/Quest/Blueprint/BlueprintNodes_Data.cpp
            src/Quest/Blueprint/BlueprintNodes_Quest.cpp
            src/Quest/Blueprint/BlueprintNodes_Dialogue.cpp
            src/Quest/Blueprint/BlueprintNodes_Inventory.cpp
            src/Quest/Blueprint/BlueprintNodes_Entity.cpp
            src/Quest/Blueprint/BlueprintNodes_World.cpp
            src/Quest/Blueprint/BlueprintNodes_Util.cpp
            src/Quest/Blueprint/BlueprintNodes_Prereq.cpp
            src/Quest/Blueprint/BlueprintNodes_GameVars.cpp
            src/Quest/Blueprint/BlueprintCompiler.cpp
            src/Quest/Blueprint/BlueprintSerialize.cpp
            src/Quest/QuestAuthoring.cpp
            ${imgui_node_editor_SOURCE_DIR}/crude_json.cpp
    )
    target_include_directories(blueprint_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${iconfontcpp_SOURCE_DIR}
            ${imgui_node_editor_SOURCE_DIR}
    )
    add_test(NAME blueprint_tests COMMAND blueprint_tests)
endif()
