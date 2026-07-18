static void fill_entity_instance_rotation(Level::PropInstance& instance,
                                          const LevelSpawnMarker& marker)
{
    if (!marker.has_rotation) {
        instance.values[7] = 1.0f;
        instance.values[9] = instance.values[10] =
            instance.values[11] = 1.0f;
        return;
    }

    const float rx = std::isfinite(marker.rot_x) ? marker.rot_x : 0.0f;
    const float ry = std::isfinite(marker.rot_y) ? marker.rot_y : 0.0f;
    const float rz = std::isfinite(marker.rot_z) ? marker.rot_z : 0.0f;
    const float sx = std::sin(rx), cx = std::cos(rx);
    const float sy = std::sin(ry), cy = std::cos(ry);
    const float sz = std::sin(rz), cz = std::cos(rz);
    float game[9] = {};
    game[0] = cy * cx;
    game[1] = sx;
    game[2] = -sy * cx;
    game[3] = sy * sz - cy * cz * sx;
    game[4] = cz * cx;
    game[5] = sy * cz * sx + cy * sz;
    game[6] = cy * sz * sx + sy * cz;
    game[7] = -sz * cx;
    game[8] = cy * cz - sy * sz * sx;
    static constexpr int kAxisMap[3] = {0, 2, 1};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            instance.values[3 + row * 3 + col] =
                game[kAxisMap[row] * 3 + kAxisMap[col]];
        }
    }
    instance.values[12] = 1.0f;
    instance.has_full_transform = true;
}

struct LevelPropStreamState {
    std::atomic<int>            phase{0};
    std::atomic<size_t>         instances_loaded{0};
    size_t                      total_instances = 0;
    size_t                      model_misses    = 0;
    std::vector<MDLMeshGeom>    geoms;
    MDLInfo                     info;
    std::thread                 worker;
    std::vector<GeneratedTerrainTexture> terrain_textures;

    std::vector<Level::PropBlock>  blocks;
    struct EntityInstance {
        Level::PropInstance instance;
        uint32_t selection_id = 0;
    };
    struct EntityBatch {
        std::vector<uint32_t> model_hashes;
        std::string label;
        std::vector<EntityInstance> instances;
    };
    std::vector<EntityBatch> entity_batches;
    std::string                    model_body_bnk;
    Gdb::SkyTheme                  sky_theme;
    Gdb::CloudTheme                cloud_theme;
    Gdb::WeatherTheme              weather_theme;
    Gdb::EnvironmentThemeTimeline  environment_timeline;
    float                          terrain_tile_size = 1.0f;
    int                            terrain_width  = 0;
    int                            terrain_height = 0;
};

static LevelPropStreamState g_level_prop_stream;
