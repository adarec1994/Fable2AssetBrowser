#include "SkyboxRenderer.h"

#ifdef _WIN32

#include "CloudRuntime.h"
#include "SkyDomeXex.h"
#include "SkyXexDecomp.h"
#include "../../UI/ModelPreview.h"
#include "../../UI/OutputLog.h"
#include "../../Utilities/State.h"
#include "../../textures/TexParser.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

using namespace DirectX;

namespace Skybox {

namespace {

#include "SkyboxRenderer/Shaders/Sources.inl"
#include "SkyboxRenderer/Shaders/Compile.inl"
#include "SkyboxRenderer/Textures/Create.inl"
#include "SkyboxRenderer/Textures/Release.inl"
#include "SkyboxRenderer/Textures/PreferredBank.inl"

}

#include "SkyboxRenderer/Frame/Camera.inl"
#include "SkyboxRenderer/Frame/Evaluate.inl"
#include "SkyboxRenderer/Resources/ClearColour.inl"
#include "SkyboxRenderer/Resources/Create.inl"
#include "SkyboxRenderer/Resources/Release.inl"
#include "SkyboxRenderer/Resources/Reset.inl"
#include "SkyboxRenderer/Rendering/Sky/Setup.inl"
#include "SkyboxRenderer/Rendering/Sky/ExactDome.inl"
#include "SkyboxRenderer/Rendering/Sky/Fallback.inl"
#include "SkyboxRenderer/Rendering/Clouds.inl"

}

#else

namespace Skybox {}

#endif
