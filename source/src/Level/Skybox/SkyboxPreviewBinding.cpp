#include "SkyboxPreviewBinding.h"

#include "EnvironmentTheme.h"
#include "SkyboxRenderer.h"
#include "../../UI/ModelPreview.h"
#include "../../UI/OutputLog.h"
#include "../../Utilities/State.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>

namespace Skybox::PreviewBinding {
namespace {

std::uint32_t EnvironmentTextureHash(std::string value, bool lowercase)
{
    if (lowercase) {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
    }
    std::replace(value.begin(), value.end(), '/', '\\');
    std::uint32_t hash = 0x811C9DC5u;
    for (unsigned char c : value) {
        hash *= 0x01000193u;
        hash ^= static_cast<std::uint32_t>(c);
    }
    return hash;
}

std::string ResolveEnvironmentTextureHash(std::uint32_t hash)
{
    if (hash == 0 || hash == 0x811C9DC5u) return {};
    for (const FlatAssetEntry& texture : S.all_tex_files) {
        if (EnvironmentTextureHash(texture.full_path, true) == hash ||
            EnvironmentTextureHash(texture.name, true) == hash ||
            EnvironmentTextureHash(texture.full_path, false) == hash ||
            EnvironmentTextureHash(texture.name, false) == hash) {
            return texture.full_path;
        }
    }
    return {};
}

std::string ResolveEnvironmentTextureHashLogged(std::uint32_t hash,
                                                const char* label)
{
    std::string name = ResolveEnvironmentTextureHash(hash);
    std::ostringstream log;
    log << "bind " << label << " hash=0x" << std::hex << hash << std::dec
        << " -> '" << name << "' (tex index size="
        << S.all_tex_files.size() << ")";
    Skybox::DebugLog(log.str().c_str());
    return name;
}

void ApplySkyTextureHashes(ModelPreview& preview, const Gdb::SkyTheme& sky)
{
#ifdef _WIN32
    auto reset_overlay = [&]() {
        if (preview.sky_overlay_srv) {
            preview.sky_overlay_srv->Release();
            preview.sky_overlay_srv = nullptr;
        }
        preview.sky_overlay_tried = false;
        preview.sky_overlay_tex_name.clear();
    };
    auto reset_moon = [&]() {
        if (preview.sky_moon_srv) {
            preview.sky_moon_srv->Release();
            preview.sky_moon_srv = nullptr;
        }
        preview.sky_moon_tried = false;
        preview.sky_moon_tex_name.clear();
        preview.sky_moon_tiles[0] = 1.0f;
        preview.sky_moon_tiles[1] = 1.0f;
    };
    auto reset_sun_disc = [&]() {
        if (preview.sky_sun_disc_srv) {
            preview.sky_sun_disc_srv->Release();
            preview.sky_sun_disc_srv = nullptr;
        }
        preview.sky_sun_disc_tried = false;
        preview.sky_sun_disc_tex_name.clear();
    };
    auto reset_glare = [&]() {
        if (preview.sky_moon_glare_srv) {
            preview.sky_moon_glare_srv->Release();
            preview.sky_moon_glare_srv = nullptr;
        }
        preview.sky_moon_glare_tried = false;
        preview.sky_moon_glare_tex_name.clear();
    };

    auto reset_sun_beams = [&preview]() {
        if (preview.sky_sun_beams_srv) {
            preview.sky_sun_beams_srv->Release();
            preview.sky_sun_beams_srv = nullptr;
        }
        preview.sky_sun_beams_tried = false;
        preview.sky_sun_beams_tex_name.clear();
    };
    auto reset_sun_glare = [&preview]() {
        if (preview.sky_sun_glare_srv) {
            preview.sky_sun_glare_srv->Release();
            preview.sky_sun_glare_srv = nullptr;
        }
        preview.sky_sun_glare_tried = false;
        preview.sky_sun_glare_tex_name.clear();
    };

    reset_overlay();
    reset_sun_disc();
    reset_moon();
    reset_glare();
    reset_sun_beams();
    reset_sun_glare();
    preview.sky_overlay_tex_name = ResolveEnvironmentTextureHashLogged(
        sky.has_sky_overlay_texture ? sky.sky_overlay_texture_hash : 0,
        "sky_overlay");
    preview.sky_sun_disc_tex_name = ResolveEnvironmentTextureHashLogged(
        sky.has_sun_disc_texture ? sky.sun_disc_texture_hash : 0,
        "sun_disc");
    preview.sky_moon_tex_name = ResolveEnvironmentTextureHashLogged(
        sky.has_moon_texture ? sky.moon_texture_hash : 0, "moon");
    preview.sky_moon_glare_tex_name = ResolveEnvironmentTextureHashLogged(
        sky.has_moon_glare_texture ? sky.moon_glare_texture_hash : 0,
        "moon_glare");
    preview.sky_sun_beams_tex_name = ResolveEnvironmentTextureHashLogged(
        sky.has_sun_beams_texture ? sky.sun_beams_texture_hash : 0,
        "sun_beams");
    preview.sky_sun_glare_tex_name = ResolveEnvironmentTextureHashLogged(
        sky.has_sun_glare_texture ? sky.sun_glare_texture_hash : 0,
        "sun_glare");
#else
    (void)preview;
    (void)sky;
#endif
}

void CopyElementParams(float (&out)[16], const Gdb::SkyTheme& sky)
{
    out[0] = sky.moon_intensity;
    out[1] = sky.moon_size;
    out[2] = sky.moon_glare_intensity;
    out[3] = sky.moon_glare_size;
    out[4] = sky.moon_transparency;
    out[5] = sky.star_brightness;
    out[6] = sky.sun_disc_size;
    out[7] = sky.sun_disc_intensity;
    out[8] = sky.sun_disc_colour[0];
    out[9] = sky.sun_disc_colour[1];
    out[10] = sky.sun_disc_colour[2];
    out[11] = sky.sun_beams_width;
    out[12] = sky.sun_beams_height;
    out[13] = sky.sun_beams_intensity;
    out[14] = sky.sun_glare_intensity;
    out[15] = sky.sun_glare_size;
}

void CopySkyThemeToKeyframe(MPSkyCloudKeyframe& key,
                            const Gdb::SkyTheme& sky)
{
    if (!sky.has_any) return;
    std::copy(std::begin(sky.sky_colour),
              std::end(sky.sky_colour),
              std::begin(key.sky_top_colour));
    if (sky.has_fog_colour) {
        std::copy(std::begin(sky.fog_colour),
                  std::end(sky.fog_colour),
                  std::begin(key.sky_bottom_colour));
    } else {
        std::copy(std::begin(sky.complementary_colour),
                  std::end(sky.complementary_colour),
                  std::begin(key.sky_bottom_colour));
    }
    std::copy(std::begin(sky.sunset_colour),
              std::end(sky.sunset_colour),
              std::begin(key.sky_sunset_colour));
    key.sky_params[0] = sky.sun_intensity;
    key.sky_params[1] = sky.complementary_bias;
    key.sky_params[2] = sky.rayleigh;
    key.sky_params[3] = sky.mie;
    std::copy(std::begin(sky.main_light_colour),
              std::end(sky.main_light_colour),
              std::begin(key.main_light_colour));
    CopyElementParams(key.element_params, sky);
}

void CopyCloudThemeToKeyframe(MPSkyCloudKeyframe& key,
                              const Gdb::CloudTheme& clouds)
{
    key.has_cloud_theme = clouds.has_any;
    key.cloud_layer_count = clouds.layer_count;
    for (int i = 0; i < 4; ++i) {
        const Gdb::CloudLayerTheme& layer = clouds.layers[i];
        key.cloud_layer[i][0] = layer.enabled ? layer.transparency : 0.0f;
        key.cloud_layer[i][1] = layer.height;
        key.cloud_layer[i][2] = layer.texture_scale_x;
        key.cloud_layer[i][3] = layer.texture_scale_y;

        key.cloud_shape[i][0] = layer.size_x;
        key.cloud_shape[i][1] = layer.size_y;
        key.cloud_shape[i][2] = 0.0f;
        key.cloud_shape[i][3] = 0.0f;

        key.cloud_motion[i][0] = layer.position_x;
        key.cloud_motion[i][1] = layer.position_y;
        key.cloud_motion[i][2] = layer.velocity_x;
        key.cloud_motion[i][3] = layer.velocity_y;

        key.cloud_light[i][0] = layer.brightness;
        key.cloud_light[i][1] = layer.ambient_light;
        key.cloud_light[i][2] = layer.normal_strength;
        key.cloud_light[i][3] = layer.translucency_strength;
        key.cloud_density_token[i] = layer.has_density_map
            ? layer.density_map_hash : 0;
        key.cloud_density_tex_name[i] =
            ResolveEnvironmentTextureHashLogged(
                layer.has_density_map ? layer.density_map_hash : 0,
                "cloud_density");
    }
}

void FillWeatherPrecipitation(float (&out)[4],
                              const Gdb::WeatherTheme& weather)
{
    out[0] = weather.has_rain
        ? std::clamp(weather.rain_density, 0.0f, 1.0f) : 0.0f;
    out[1] = weather.has_rain
        ? std::clamp(weather.rain_size, 0.0f, 4.0f) : 0.0f;
    out[2] = weather.has_snow
        ? std::max(weather.snow_fall_speed, 0.0f) : 0.0f;
    out[3] = weather.has_snow
        ? std::max(weather.snow_size, 0.0f) : 0.0f;
}

void FillFogFromSky(float (&range)[4], float (&density)[2],
                    bool& has_fog, const Gdb::SkyTheme& sky)
{
    has_fog = sky.has_any && (sky.has_near_fog || sky.has_far_fog);
    range[0] = sky.has_fogging_start
        ? std::max(sky.fogging_start, 0.0f) : 0.0f;
    range[1] = std::max(sky.near_distance, 0.0f);
    range[2] = std::max(sky.far_distance, 0.0f);
    range[3] = std::max(sky.close_fog_max_distance, 0.0f);
    density[0] = std::clamp(sky.near_density, 0.0f, 1.0f);
    density[1] = std::clamp(sky.far_density, 0.0f, 1.0f);
}

void CopyWeatherThemeToKeyframe(MPSkyCloudKeyframe& key,
                                const Gdb::WeatherTheme& weather,
                                const Gdb::SkyTheme& sky)
{
    if (weather.has_any) {
        key.has_weather_theme = true;
        FillWeatherPrecipitation(key.weather_precip, weather);
        key.weather_mist_strength = weather.has_ground_mist
            ? std::clamp(weather.ground_mist_strength, 0.0f, 4.0f)
            : 0.0f;
    }
    if (sky.has_any && (sky.has_near_fog || sky.has_far_fog)) {
        FillFogFromSky(key.fog_range, key.fog_density,
                       key.has_fog_theme, sky);
    }
}

}

void ApplySkyTheme(ModelPreview& preview, const Gdb::SkyTheme& sky)
{
    preview.has_sky_theme = sky.has_any;
    if (!preview.has_sky_theme) return;

    std::copy(std::begin(sky.sky_colour),
              std::end(sky.sky_colour),
              std::begin(preview.sky_top_colour));
    if (sky.has_fog_colour) {
        std::copy(std::begin(sky.fog_colour),
                  std::end(sky.fog_colour),
                  std::begin(preview.sky_bottom_colour));
    } else {
        std::copy(std::begin(sky.complementary_colour),
                  std::end(sky.complementary_colour),
                  std::begin(preview.sky_bottom_colour));
    }
    std::copy(std::begin(sky.sunset_colour),
              std::end(sky.sunset_colour),
              std::begin(preview.sky_sunset_colour));
    preview.sky_params[0] = sky.sun_intensity;
    preview.sky_params[1] = sky.complementary_bias;
    preview.sky_params[2] = sky.rayleigh;
    preview.sky_params[3] = sky.mie;
    std::copy(std::begin(sky.main_light_colour),
              std::end(sky.main_light_colour),
              std::begin(preview.main_light_colour));
    preview.sky_time_of_day = sky.source_time_of_day;
    preview.has_sun_axis = sky.has_sun_axis;
    preview.sun_axis[0] = std::isfinite(sky.sun_axis_elevation)
        ? sky.sun_axis_elevation : 26.0f;
    preview.sun_axis[1] = std::isfinite(sky.sun_axis_z_offset)
        ? sky.sun_axis_z_offset : 0.0f;
    preview.sun_axis[2] = std::isfinite(sky.sun_axis_xy_rotation)
        ? sky.sun_axis_xy_rotation : 0.0f;
    preview.sun_axis[3] =
        (std::isfinite(sky.main_light_time_factor) &&
         sky.main_light_time_factor > 0.0f)
            ? sky.main_light_time_factor : 1.0f;
    CopyElementParams(preview.sky_element_params, sky);
    preview.has_moon_axis = sky.has_moon_axis;
    preview.moon_axis[0] = std::isfinite(sky.moon_axis_elevation)
        ? sky.moon_axis_elevation : 26.0f;
    preview.moon_axis[1] = std::isfinite(sky.moon_axis_z_offset)
        ? sky.moon_axis_z_offset : 0.0f;
    preview.moon_axis[2] = std::isfinite(sky.moon_axis_xy_rotation)
        ? sky.moon_axis_xy_rotation : 0.0f;
    ApplySkyTextureHashes(preview, sky);
}

void ApplyCloudTheme(ModelPreview& preview, const Gdb::CloudTheme& clouds)
{
    preview.has_cloud_theme = clouds.has_any;
    preview.cloud_layer_count = clouds.layer_count;
    preview.cloud_runtime_initialised = false;
    int density_resolved = 0;
    int density_missing = 0;
    std::ostringstream layer_log;
    layer_log << "cloud theme layers:";
    for (int i = 0; i < 4; ++i) {
        const Gdb::CloudLayerTheme& layer = clouds.layers[i];
        const float opacity = layer.transparency;
        preview.cloud_layer[i][0] =
            layer.enabled ? layer.transparency : 0.0f;
        preview.cloud_layer[i][1] = layer.height;
        preview.cloud_layer[i][2] = layer.texture_scale_x;
        preview.cloud_layer[i][3] = layer.texture_scale_y;

        preview.cloud_shape[i][0] = layer.size_x;
        preview.cloud_shape[i][1] = layer.size_y;
        preview.cloud_shape[i][2] = 0.0f;
        preview.cloud_shape[i][3] = 0.0f;

        preview.cloud_motion[i][0] = layer.position_x;
        preview.cloud_motion[i][1] = layer.position_y;
        preview.cloud_motion[i][2] = layer.velocity_x;
        preview.cloud_motion[i][3] = layer.velocity_y;

        preview.cloud_light[i][0] = layer.brightness;
        preview.cloud_light[i][1] = layer.ambient_light;
        preview.cloud_light[i][2] = layer.normal_strength;
        preview.cloud_light[i][3] = layer.translucency_strength;

#ifdef _WIN32
        if (preview.cloud_density_srv[i]) {
            preview.cloud_density_srv[i]->Release();
            preview.cloud_density_srv[i] = nullptr;
        }
        preview.cloud_density_tried[i] = false;
#endif
        preview.cloud_density_tex_name[i].clear();
        preview.cloud_density_token[i] = layer.has_density_map
            ? layer.density_map_hash : 0;
        if (layer.enabled && layer.has_density_map) {
            preview.cloud_density_tex_name[i] =
                ResolveEnvironmentTextureHashLogged(
                    layer.density_map_hash, "cloud_density_base");
            if (!preview.cloud_density_tex_name[i].empty()) {
                ++density_resolved;
            } else {
                ++density_missing;
            }
        }
        if (layer.enabled) {
            layer_log << " L" << (i + 1)
                      << "(alpha=" << std::fixed << std::setprecision(2)
                      << opacity
                      << ", height=" << std::setprecision(1)
                      << layer.height
                      << ", size=" << layer.size_x << 'x'
                      << layer.size_y
                      << ", scale=" << std::setprecision(3)
                      << layer.texture_scale_x << 'x'
                      << layer.texture_scale_y << ')';
        }
    }
    if (clouds.has_any) {
        std::ostringstream summary;
        summary << "cloud theme: density maps resolved=" << density_resolved
                << " missing=" << density_missing;
        OutputLog::info(summary.str());
        OutputLog::info(layer_log.str());
    }
}

void ApplyWeatherTheme(ModelPreview& preview,
                       const Gdb::WeatherTheme& weather,
                       const Gdb::SkyTheme& sky)
{
    preview.has_weather_theme = weather.has_any;
    FillWeatherPrecipitation(preview.weather_precip, weather);
    preview.weather_mist_strength = weather.has_ground_mist
        ? std::clamp(weather.ground_mist_strength, 0.0f, 4.0f) : 0.0f;

    if (weather.has_wind) {
        const float azimuth =
            0.5f * (weather.wind_xy_rotation_min +
                    weather.wind_xy_rotation_max);
        const float strength =
            0.5f * (weather.wind_strength_min +
                    weather.wind_strength_max);
        const float elevation =
            0.5f * (weather.wind_elevation_min +
                    weather.wind_elevation_max);
        preview.weather_wind[0] = std::isfinite(azimuth) ? azimuth : 0.0f;
        preview.weather_wind[1] = std::isfinite(strength)
            ? std::clamp(strength, 0.0f, 40.0f) : 0.0f;
        preview.weather_wind[2] =
            std::isfinite(elevation) ? elevation : 0.0f;
        preview.weather_wind[3] =
            std::isfinite(weather.wind_direction_variation)
                ? weather.wind_direction_variation : 0.0f;
    } else {
        preview.weather_wind[0] = 0.0f;
        preview.weather_wind[1] = 2.0f;
        preview.weather_wind[2] = 0.0f;
        preview.weather_wind[3] = 0.0f;
    }

    FillFogFromSky(preview.fog_range, preview.fog_density,
                   preview.has_fog_theme, sky);

    if (weather.has_any) {
        std::ostringstream summary;
        summary << "weather preview: rain="
                << std::fixed << std::setprecision(2)
                << preview.weather_precip[0]
                << " snow="
                << (preview.weather_precip[2] > 0.0f &&
                    preview.weather_precip[3] > 0.0f ? "yes" : "no")
                << " wind=" << preview.weather_wind[1]
                << " mist=" << preview.weather_mist_strength
                << (preview.has_fog_theme ? " fog=theme" : "");
        OutputLog::info(summary.str());
    }
}

void ApplyEnvironmentTimeline(
    ModelPreview& preview,
    const Gdb::EnvironmentThemeTimeline& timeline)
{
    preview.has_day_night_cycle =
        timeline.has_any && timeline.keyframes.size() >= 2;
    preview.day_night_keyframes.clear();
    if (!preview.has_day_night_cycle) return;

    for (const Gdb::EnvironmentThemeKeyframe& source : timeline.keyframes) {
        MPSkyCloudKeyframe key;
        key.time_of_day = std::isfinite(source.time_of_day)
            ? source.time_of_day - std::floor(source.time_of_day)
            : 0.0f;
        if (key.time_of_day < 0.0f) key.time_of_day += 1.0f;
        std::copy(std::begin(preview.sky_top_colour),
                  std::end(preview.sky_top_colour),
                  std::begin(key.sky_top_colour));
        std::copy(std::begin(preview.sky_bottom_colour),
                  std::end(preview.sky_bottom_colour),
                  std::begin(key.sky_bottom_colour));
        std::copy(std::begin(preview.sky_sunset_colour),
                  std::end(preview.sky_sunset_colour),
                  std::begin(key.sky_sunset_colour));
        std::copy(std::begin(preview.sky_params),
                  std::end(preview.sky_params),
                  std::begin(key.sky_params));
        key.has_weather_theme = preview.has_weather_theme;
        std::copy(std::begin(preview.weather_precip),
                  std::end(preview.weather_precip),
                  std::begin(key.weather_precip));
        key.weather_mist_strength = preview.weather_mist_strength;
        key.has_fog_theme = preview.has_fog_theme;
        std::copy(std::begin(preview.fog_range),
                  std::end(preview.fog_range),
                  std::begin(key.fog_range));
        std::copy(std::begin(preview.fog_density),
                  std::end(preview.fog_density),
                  std::begin(key.fog_density));
        CopySkyThemeToKeyframe(key, source.sky);
        CopyCloudThemeToKeyframe(key, source.clouds);
        CopyWeatherThemeToKeyframe(key, source.weather, source.sky);
#ifdef _WIN32
        if (preview.sky_overlay_tex_name.empty() &&
            source.sky.has_sky_overlay_texture) {
            preview.sky_overlay_tex_name = ResolveEnvironmentTextureHash(
                source.sky.sky_overlay_texture_hash);
            preview.sky_overlay_tried = false;
        }
        if (preview.sky_sun_disc_tex_name.empty() &&
            source.sky.has_sun_disc_texture) {
            preview.sky_sun_disc_tex_name = ResolveEnvironmentTextureHash(
                source.sky.sun_disc_texture_hash);
            preview.sky_sun_disc_tried = false;
        }
        if (preview.sky_moon_tex_name.empty() &&
            source.sky.has_moon_texture) {
            preview.sky_moon_tex_name = ResolveEnvironmentTextureHash(
                source.sky.moon_texture_hash);
            preview.sky_moon_tried = false;
        }
        if (preview.sky_moon_glare_tex_name.empty() &&
            source.sky.has_moon_glare_texture) {
            preview.sky_moon_glare_tex_name = ResolveEnvironmentTextureHash(
                source.sky.moon_glare_texture_hash);
            preview.sky_moon_glare_tried = false;
        }
        if (preview.sky_sun_beams_tex_name.empty() &&
            source.sky.has_sun_beams_texture) {
            preview.sky_sun_beams_tex_name = ResolveEnvironmentTextureHash(
                source.sky.sun_beams_texture_hash);
            preview.sky_sun_beams_tried = false;
        }
        if (preview.sky_sun_glare_tex_name.empty() &&
            source.sky.has_sun_glare_texture) {
            preview.sky_sun_glare_tex_name = ResolveEnvironmentTextureHash(
                source.sky.sun_glare_texture_hash);
            preview.sky_sun_glare_tried = false;
        }
#endif
        preview.day_night_keyframes.push_back(key);
    }

    preview.has_sky_theme = true;
    std::ostringstream summary;
    summary << "day/night cycle: preview timeline active keyframes="
            << preview.day_night_keyframes.size()
            << " cycle=" << std::fixed << std::setprecision(0)
            << preview.day_night_cycle_seconds << "s";
    OutputLog::info(summary.str());
}

}
