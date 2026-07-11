#pragma once

#include <cstdint>

struct ModelPreview;
struct FlyCam;

#ifdef _WIN32
struct ID3D11Device;
struct ID3D11DeviceContext;

namespace DirectX {
struct XMMATRIX;
}
#endif

namespace Skybox {

// Appends a line to sky_dome_debug.txt (diagnostics for the byte-derived
// sky path); no-op when the file cannot be opened.
void DebugLog(const char* line);

struct CameraFrame {
    float view_forward[3] = {};
    float forward[3] = {};
    float right[3] = {};
    float up[3] = {};
    float fov_radians = 0.0f;
    float aspect = 1.0f;
    float tan_half_x = 0.0f;
    float tan_half_y = 0.0f;
};

struct FrameState {
    // default.xex keeps the absolute cloud clock as a double and rounds only
    // the post-subtraction delta to single precision.
    double elapsed_time = 0.0;
    float time_of_day = 0.5f;

    float sky_top[3] = {};
    float sky_bottom[3] = {};
    float sky_sunset[3] = {};
    float sky_params[4] = {};
    float main_light_colour[3] = {};

    bool has_cloud_theme = false;
    float cloud_layer[4][4] = {};
    float cloud_shape[4][4] = {};
    float cloud_motion[4][4] = {};
    float cloud_light[4][4] = {};
    std::uint32_t cloud_density_token[4] = {};

    float weather[4] = {};
    float mist = 0.0f;
    float fog_range[4] = {};
    float fog_density[2] = {};
    bool has_fog = false;

    float sun_direction[3] = {};
    float moon_direction[3] = {};
    // Same layout as Skybox::Keyframe::element_params.
    float element_params[16] = {1.0f, 1.0f, 0.0f, 1.0f,
                                1.0f, 0.0f, 1.0f, 1.0f,
                                1.0f, 1.0f, 1.0f, 1.0f,
                                1.0f, 0.0f, 0.0f, 1.0f};
};

CameraFrame BuildCameraFrame(const FlyCam& camera, int width, int height);
FrameState EvaluateFrame(ModelPreview& preview);
void GetClearColour(const ModelPreview& preview,
                    const FrameState& frame,
                    float out_rgba[4]);
#ifdef _WIN32
void CreateD3D11Resources(ID3D11Device* device, ModelPreview& preview);
void ReleaseD3D11Resources(ModelPreview& preview);
void ResetForBuild(ModelPreview& preview);

void DrawSky(ID3D11Device* device,
             ID3D11DeviceContext* context,
             ModelPreview& preview,
             const CameraFrame& camera,
             const FrameState& frame);

void DrawClouds(ID3D11DeviceContext* context,
                ModelPreview& preview,
                const FlyCam& camera,
                const CameraFrame& camera_frame,
                const FrameState& frame,
                const DirectX::XMMATRIX& view,
                const DirectX::XMMATRIX& projection);
#endif

}  // namespace Skybox
