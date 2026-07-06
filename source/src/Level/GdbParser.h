#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace Gdb {

struct Placement {
    float    x, y, z;
    float    yaw;
    float    rot_x, rot_y, rot_z;
    float    scale;
    bool     has_rotation;
    uint32_t marker;
    uint32_t hash_a;
    uint32_t parent_hash;
    uint32_t model_path_hash;
    uint32_t skeleton_file_hash;
    uint32_t retarget_skeleton_file_hash;
    std::vector<uint32_t> model_path_hashes;
    bool     indexed_record;
    bool     transform_from_indexed_record;
    std::string entity_name;
};

struct GdbInfo {
    std::vector<Placement> placements;
};

struct WaterTheme {
    bool has_any = false;
    bool has_shallow_colour = false;
    bool has_deep_colour = false;
    bool has_edge_blend_min = false;
    bool has_edge_blend_max = false;
    bool has_edge_blend_bias = false;
    bool has_max_refraction_distance = false;
    bool has_fresnel_bias = false;
    bool has_reflection_strength = false;
    bool has_refraction_scale = false;
    bool has_reflection_scale = false;
    bool has_reflection_bias = false;
    bool has_normal_scale = false;

    float shallow_colour[3] = {0.155f, 0.285f, 0.235f};
    float deep_colour[3] = {0.010f, 0.075f, 0.085f};
    float opacity = 0.42f;
    float edge_blend_min = 0.0f;
    float edge_blend_max = 1.0f;
    float edge_blend_bias = 0.0f;
    float max_refraction_distance = 25.0f;
    float fresnel_bias = 0.0f;
    float reflection_strength = 0.34f;
    float refraction_scale = 1.0f;
    float reflection_scale = 1.0f;
    float reflection_bias = 0.0f;
    float normal_scale = 1.0f;
    float source_time_of_day = -1.0f;
};

struct SkyTheme {
    bool has_any = false;
    bool has_sky_colour = false;
    bool has_complementary_colour = false;
    bool has_sunset_colour = false;
    bool has_fog_colour = false;
    bool has_sun_intensity = false;
    bool has_rayleigh = false;
    bool has_mie = false;
    bool has_complementary_bias = false;
    bool has_near_fog = false;
    bool has_far_fog = false;
    bool has_sky_overlay_texture = false;
    bool has_moon_texture = false;
    bool has_moon_glare_texture = false;
    bool has_sun_disc_texture = false;

    float sky_colour[3] = {0.42f, 0.56f, 0.76f};
    float complementary_colour[3] = {0.72f, 0.64f, 0.54f};
    float sunset_colour[3] = {1.0f, 0.47f, 0.22f};
    float fog_colour[3] = {0.48f, 0.55f, 0.62f};
    float sun_intensity = 1.0f;
    float rayleigh = 1.0f;
    float mie = 1.0f;
    float complementary_bias = 0.35f;
    float near_distance = 0.0f;
    float near_density = 0.0f;
    float far_distance = 0.0f;
    float far_density = 0.0f;
    float close_fog_max_distance = 0.0f;
    float fogging_start = 0.0f;
    bool has_fogging_start = false;
    bool has_sun_axis = false;
    float sun_axis_elevation = 26.0f;
    float sun_axis_z_offset = 0.0f;
    float sun_axis_xy_rotation = 0.0f;
    float main_light_time_factor = 1.0f;
    float source_time_of_day = -1.0f;
    uint32_t sky_overlay_texture_hash = 0;
    uint32_t moon_texture_hash = 0;
    uint32_t moon_glare_texture_hash = 0;
    uint32_t sun_disc_texture_hash = 0;
};

struct CloudLayerTheme {
    bool enabled = false;
    bool has_density_map = false;
    bool has_position = false;
    bool has_size = false;
    bool has_texture_scale = false;
    bool has_velocity = false;
    bool has_height = false;
    bool has_transparency = false;
    bool has_brightness = false;
    bool has_ambient = false;
    bool has_normal_strength = false;
    bool has_translucency_strength = false;

    uint32_t density_map_hash = 0;
    float position_x = 0.0f;
    float position_y = 0.0f;
    float size_x = 1.0f;
    float size_y = 1.0f;
    float texture_scale_x = 2.5f;
    float texture_scale_y = 2.0f;
    float velocity_x = 0.015f;
    float velocity_y = 0.006f;
    float height = 350.0f;
    float transparency = 0.35f;
    float brightness = 0.65f;
    float ambient_light = 0.45f;
    float normal_strength = 0.45f;
    float translucency_strength = 0.35f;
};

struct CloudTheme {
    bool has_any = false;
    int layer_count = 0;
    CloudLayerTheme layers[4];
    float source_time_of_day = -1.0f;
};

struct WeatherTheme {
    bool has_any = false;
    bool has_rain = false;
    bool has_snow = false;
    bool has_wind = false;
    bool has_ground_mist = false;

    float rain_density = 0.0f;
    float rain_size = 0.0f;
    float snow_fall_speed = 0.0f;
    float snow_size = 0.0f;

    float wind_strength_min = 0.0f;
    float wind_strength_max = 0.0f;
    float wind_strength_variation = 0.0f;
    float wind_xy_rotation_min = 0.0f;
    float wind_xy_rotation_max = 0.0f;
    float wind_elevation_min = 0.0f;
    float wind_elevation_max = 0.0f;
    float wind_change_frequency = 0.0f;
    float wind_change_duration = 0.0f;
    float wind_direction_variation = 0.0f;

    float ground_mist_strength = 0.0f;
    float source_time_of_day = -1.0f;
};

struct EnvironmentThemeKeyframe {
    float time_of_day = 0.0f;
    WaterTheme water;
    SkyTheme sky;
    CloudTheme clouds;
    WeatherTheme weather;
};

struct EnvironmentThemeTimeline {
    bool has_any = false;
    std::vector<EnvironmentThemeKeyframe> keyframes;
};

struct RecordRow {
    uint32_t index = 0;
    uint32_t hash = 0;
    uint32_t parent_hash = 0;
    uint32_t model_path_hash = 0;
    uint32_t skeleton_file_hash = 0;
    uint32_t retarget_skeleton_file_hash = 0;
    std::vector<uint32_t> model_path_hashes;
    std::string name;
    std::string skeleton_file_name;
    std::string retarget_skeleton_file_name;
};

GdbInfo Parse(const std::vector<uint8_t>& bytes);

GdbInfo ParseWithSaveMap(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name);

bool ExtractWaterTheme(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    WaterTheme& out_theme);

bool ExtractSkyTheme(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    SkyTheme& out_theme);

bool ExtractCloudTheme(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    CloudTheme& out_theme);

bool ExtractWeatherTheme(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    WeatherTheme& out_theme);

bool ExtractEnvironmentThemeTimeline(
    const std::vector<const std::vector<uint8_t>*>& gdbs,
    EnvironmentThemeTimeline& out_timeline);

bool LookupModelPathHash(
    const std::vector<uint8_t>& bytes,
    uint32_t record_hash,
    uint32_t& out_model_path_hash,
    uint32_t* out_parent_hash = nullptr);

bool LookupModelPathHashes(
    const std::vector<uint8_t>& bytes,
    uint32_t record_hash,
    std::vector<uint32_t>& out_model_path_hashes,
    uint32_t* out_parent_hash = nullptr);

bool LookupPlacement(
    const std::vector<uint8_t>& bytes,
    uint32_t record_hash,
    const std::string& entity_name,
    Placement& out_placement);

std::vector<RecordRow> Build010RecordRows(
    const std::vector<uint8_t>& bytes,
    const std::vector<std::pair<uint32_t, std::string>>& hash_to_name);

}
