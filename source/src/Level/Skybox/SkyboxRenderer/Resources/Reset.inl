void ResetForBuild(ModelPreview& preview)
{
    preview.has_sky_theme = false;
    preview.main_light_colour[0] = 1.0f;
    preview.main_light_colour[1] = 1.0f;
    preview.main_light_colour[2] = 1.0f;
    preview.has_cloud_theme = false;
    preview.cloud_layer_count = 0;
    preview.cloud_runtime_initialised = false;
    for (int i = 0; i < 4; ++i) {
        preview.cloud_layer[i][0] = 0.0f;
        preview.cloud_layer[i][1] = 0.0f;
        preview.cloud_layer[i][2] = 0.001f;
        preview.cloud_layer[i][3] = 0.001f;
        std::fill(std::begin(preview.cloud_shape[i]),
                  std::end(preview.cloud_shape[i]), 0.0f);
        std::fill(std::begin(preview.cloud_motion[i]),
                  std::end(preview.cloud_motion[i]), 0.0f);
        std::fill(std::begin(preview.cloud_light[i]),
                  std::end(preview.cloud_light[i]), 0.0f);
        preview.cloud_density_token[i] = 0;
        CloudRuntime::InitialiseLayer(preview.cloud_runtime[i]);
    }
    preview.sky_time_of_day = -1.0f;
    preview.has_day_night_cycle = false;
    preview.day_night_keyframes.clear();
    preview.time_of_day_override = false;
    preview.time_of_day_override_value = 0.5f;
    preview.current_time_of_day = 0.5f;
    ReleaseCloudTextures(preview);
    ReleaseSkyTextures(preview);
}
