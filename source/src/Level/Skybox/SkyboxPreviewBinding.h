#pragma once

struct ModelPreview;

namespace Gdb {
struct CloudTheme;
struct EnvironmentThemeTimeline;
struct SkyTheme;
struct WeatherTheme;
}

namespace Skybox::PreviewBinding {

void ApplySkyTheme(ModelPreview& preview, const Gdb::SkyTheme& sky);
void ApplyCloudTheme(ModelPreview& preview, const Gdb::CloudTheme& clouds);
void ApplyWeatherTheme(ModelPreview& preview,
                       const Gdb::WeatherTheme& weather,
                       const Gdb::SkyTheme& sky);
void ApplyEnvironmentTimeline(
    ModelPreview& preview,
    const Gdb::EnvironmentThemeTimeline& timeline);

}
