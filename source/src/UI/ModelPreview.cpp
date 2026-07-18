#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>
#include <cstdarg>
#include <filesystem>
#include <optional>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <sstream>
#include <limits>
#include <mutex>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <chrono>
#include "ModelPreview.h"
#include "../Level/Terrain/TerrainPaint.h"
#include "../Level/Terrain/TerrainSplat.h"
#include "../Level/Skybox/SkyboxRenderer.h"
extern std::vector<Fx::Placement> g_pending_level_fx;
extern Fx::Bank                   g_particle_bank;
extern bool                       g_particle_bank_loaded;
#include "../Utilities/Files.h"
#include "../Utilities/Utils.h"
#include "../Utilities/State.h"
#include "../BNKCore.cpp"
#include "../textures/TexParser.h"
#include "../textures/LhTexCodec.h"
#include "OutputLog.h"
#include <zlib.h>
#ifdef _WIN32
#include <initguid.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
using namespace DirectX;
#else
#include <GL/glew.h>
#endif
FlyCam g_flycam;
#include "ModelPreview/Common/Helpers.inl"
#include "ModelPreview/Textures/BlockCompression.inl"
#include "ModelPreview/Textures/XboxTiling.inl"
#include "ModelPreview/Textures/Decoder.inl"
#include "ModelPreview/Textures/Lookup.inl"
#include "ModelPreview/Camera/FlyCam.inl"
#ifdef _WIN32
#include "ModelPreview/DirectX/Resources/Release.inl"
#include "ModelPreview/DirectX/Shaders/Shaders.inl"
#include "ModelPreview/DirectX/Resources/Textures.inl"
#include "ModelPreview/DirectX/Resources/RenderTarget.inl"
#include "ModelPreview/DirectX/Pipeline/Pipeline.inl"
#include "ModelPreview/DirectX/Lifecycle.inl"
#include "ModelPreview/DirectX/Particles.inl"
#include "ModelPreview/DirectX/Resources/TextureCache.inl"
#include "ModelPreview/DirectX/Animation/Skeleton.inl"
#include "ModelPreview/DirectX/Animation/Cloth.inl"
#include "ModelPreview/DirectX/Model/Build.inl"
#include "ModelPreview/DirectX/Rendering/Frame.inl"
#include "ModelPreview/DirectX/Animation/WorldPose.inl"

#else
#include "ModelPreview/OpenGL/Resources.inl"
#include "ModelPreview/OpenGL/Lifecycle.inl"
#include "ModelPreview/OpenGL/Textures.inl"
#include "ModelPreview/OpenGL/Model/Build.inl"
#include "ModelPreview/OpenGL/Math/Matrices.inl"
#include "ModelPreview/OpenGL/Rendering/Frame.inl"
#endif
