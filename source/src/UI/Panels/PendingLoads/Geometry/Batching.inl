static std::string make_combined_prop_name(const std::string& model_path,
                                           const std::string& src_name,
                                           size_t              instance_count,
                                           uint32_t            block_type)
{
    std::string base = model_path;
    const size_t sl = base.find_last_of("/\\");
    if (sl != std::string::npos) base = base.substr(sl + 1);

    const bool is_engine_level = (block_type == 2u) || (block_type == 21u);
    const bool is_entity = block_type == 0xE3u;
    std::string name = is_entity
        ? std::string("entity: ") + base
        : (is_engine_level ? std::string("engine_level: ") + base
                           : std::string("prop: ") + base);
    if (!src_name.empty()) name += "#" + src_name;
    name += " (" + std::to_string(instance_count) + " inst)";
    return name;
}

constexpr size_t kMaxCombinedPropVertices = 250000;
constexpr size_t kMaxCombinedPropIndices  = 750000;

static void init_combined_prop_geom(MDLMeshGeom& dst,
                                    const MDLMeshGeom& src,
                                    const std::string& model_path,
                                    size_t instance_count,
                                    uint32_t block_type,
                                    size_t part_index)
{
    dst = MDLMeshGeom{};
    dst.diffuse_tex_name  = src.diffuse_tex_name;
    dst.normal_tex_name   = src.normal_tex_name;
    dst.specular_tex_name = src.specular_tex_name;
    dst.metallic_tex_name = src.metallic_tex_name;
    dst.extra_tex_name    = src.extra_tex_name;
    dst.MeshIndex         = src.MeshIndex;
    dst.SubMeshIndex      = src.SubMeshIndex;
    dst.name = make_combined_prop_name(
        model_path, src.name, instance_count, block_type);
    if (part_index > 0) {
        dst.name += " part " + std::to_string(part_index + 1);
    }

    size_t reserve_instances = instance_count;
    const size_t src_vertices = src.positions.size() / 3;
    if (src_vertices > 0) {
        reserve_instances = std::min(
            reserve_instances,
            std::max<size_t>(1, kMaxCombinedPropVertices / src_vertices));
    }
    if (!src.indices.empty()) {
        reserve_instances = std::min(
            reserve_instances,
            std::max<size_t>(1, kMaxCombinedPropIndices / src.indices.size()));
    }
    if (reserve_instances > 0) {
        dst.positions.reserve(src.positions.size() * reserve_instances);
        dst.normals.reserve(src.normals.size() * reserve_instances);
        dst.uvs.reserve(src.uvs.size() * reserve_instances);
        dst.bone_ids.reserve(src.bone_ids.size() * reserve_instances);
        dst.bone_weights.reserve(src.bone_weights.size() * reserve_instances);
        dst.indices.reserve(src.indices.size() * reserve_instances);
        dst.pick_ranges.reserve(reserve_instances);
    }
}

static bool would_exceed_combined_prop_limits(const MDLMeshGeom& dst,
                                              const MDLMeshGeom& src)
{
    if (dst.positions.empty()) return false;
    const size_t dst_vertices = dst.positions.size() / 3;
    const size_t src_vertices = src.positions.size() / 3;
    if (dst_vertices + src_vertices > kMaxCombinedPropVertices) return true;
    if (dst.indices.size() + src.indices.size() > kMaxCombinedPropIndices) {
        return true;
    }
    return false;
}

static void flush_combined_prop_geom(std::vector<MDLMeshGeom>& out,
                                     MDLMeshGeom& geom,
                                     const MDLMeshGeom& src,
                                     const std::string& model_path,
                                     size_t instance_count,
                                     uint32_t block_type,
                                     size_t& part_index)
{
    if (!geom.positions.empty() && !geom.indices.empty()) {
        out.push_back(std::move(geom));
        ++part_index;
    }
    init_combined_prop_geom(geom, src, model_path, instance_count,
                            block_type, part_index);
}

static void normalize_grid_uvs(MDLMeshGeom& geom, uint32_t width, uint32_t height)
{
    if (width < 2 || height < 2) return;
    const size_t vertex_count = size_t(width) * size_t(height);
    if (geom.uvs.size() < vertex_count * 2) return;

    for (uint32_t y = 0; y < height; ++y) {
        const float v = float(y) / float(height - 1);
        for (uint32_t x = 0; x < width; ++x) {
            const size_t i = size_t(y) * width + x;
            geom.uvs[i * 2 + 0] = float(x) / float(width - 1);
            geom.uvs[i * 2 + 1] = v;
        }
    }
}
