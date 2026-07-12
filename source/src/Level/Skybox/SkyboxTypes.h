#pragma once

#include <cstdint>
#include <string>

namespace Skybox {

struct Keyframe {
    float time_of_day = 0.0f;
    float sky_top_colour[3] = {0.42f, 0.56f, 0.76f};
    float sky_bottom_colour[3] = {0.55f, 0.60f, 0.65f};
    float sky_sunset_colour[3] = {1.0f, 0.47f, 0.22f};
    float sky_params[4] = {1.0f, 0.35f, 1.0f, 1.0f};
    float main_light_colour[3] = {1.0f, 1.0f, 1.0f};

    float element_params[16] = {1.0f, 1.0f, 0.0f, 1.0f,
                                1.0f, 0.0f, 1.0f, 1.0f,
                                1.0f, 1.0f, 1.0f, 1.0f,
                                1.0f, 0.0f, 0.0f, 1.0f};
    bool has_cloud_theme = false;
    int cloud_layer_count = 0;
    float cloud_layer[4][4] = {};
    float cloud_shape[4][4] = {};
    float cloud_motion[4][4] = {};
    float cloud_light[4][4] = {};
    std::uint32_t cloud_density_token[4] = {};
    std::string cloud_density_tex_name[4];
    bool has_weather_theme = false;
    float weather_precip[4] = {};
    float weather_mist_strength = 0.0f;
    bool has_fog_theme = false;
    float fog_range[4] = {};
    float fog_density[2] = {};
};

}

using MPSkyCloudKeyframe = Skybox::Keyframe;
