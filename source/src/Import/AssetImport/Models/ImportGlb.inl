bool import_glb(const std::string& glb_path, const Options& opt,
                Result& res, std::string& err)
{
    DebugLog::Scope debug_scope("Import GLB", glb_path);
    res = Result{};
    const std::string stem =
        std::filesystem::path(glb_path).stem().string();
    const std::string asset =
        sanitize_name(opt.asset_name.empty() ? stem : opt.asset_name);
    std::string folder = normalize_folder(
        opt.dest_folder.empty() ? ("art\\imported\\" + asset) : opt.dest_folder);

    progress_update(0, 100, "Reading " + std::filesystem::path(glb_path)
                                              .filename().string());

    GlbImport::Scene scene;
    if (!GlbImport::load_glb(glb_path, scene, err)) return false;

    if (!opt.material_textures.empty()) {
        if (opt.material_textures.size() != scene.materials.size()) {
            err = "GLB material assignments no longer match the source file";
            return false;
        }
        auto valid_image = [&](int idx) {
            return idx == -1 ||
                   (idx >= 0 && size_t(idx) < scene.images.size());
        };
        for (size_t mi = 0; mi < scene.materials.size(); ++mi) {
            const auto& assignment = opt.material_textures[mi];
            for (const auto& slot : {
                     std::pair<const char*, int>{"diffuse", assignment.diffuse},
                     {"normal", assignment.normal},
                     {"specular", assignment.specular},
                     {"metallic", assignment.metallic},
                     {"extra", assignment.extra}}) {
                if (!valid_image(slot.second)) {
                    err = "material '" + scene.materials[mi].name +
                          "' has an invalid " + slot.first +
                          " texture selection";
                    return false;
                }
            }
            auto& material = scene.materials[mi];
            material.base_color = assignment.diffuse;
            material.normal = assignment.normal;
            material.occlusion = assignment.specular;
            material.metallic_rough = assignment.metallic;
            material.emissive = assignment.extra;
        }
    }

    for (const auto& material : scene.materials) {
        for (const auto& slot : {
                 std::pair<const char*, int>{"diffuse", material.base_color},
                 {"normal", material.normal},
                 {"specular", material.occlusion},
                 {"metallic", material.metallic_rough},
                 {"extra", material.emissive}}) {
            if (slot.second < -1 ||
                (slot.second >= 0 &&
                 size_t(slot.second) >= scene.images.size())) {
                err = "material '" + material.name + "' references an invalid " +
                      slot.first + " texture";
                return false;
            }
        }
    }

    std::set<int> generic_images;
    std::set<int> normal_images;
    for (const auto& material : scene.materials) {
        if (material.normal >= 0) normal_images.insert(material.normal);
        for (int idx : {material.occlusion, material.metallic_rough,
                        material.emissive}) {
            if (idx >= 0) generic_images.insert(idx);
        }
        if (material.base_color >= 0) {
            generic_images.insert(material.base_color);
        }
    }

    std::vector<PendingEntry> pending;
    std::set<std::string> used_names;
    std::vector<std::string> image_vpath(scene.images.size());
    std::vector<std::string> normal_vpath(scene.images.size());
    std::vector<ImageLoad::Image> decoded_images(scene.images.size());
    std::vector<bool> image_decoded(scene.images.size(), false);

    int prog = 5;
    progress_update(prog, 100, "Encoding textures");
    auto decode_image = [&](int idx) -> ImageLoad::Image* {
        if (idx < 0 || size_t(idx) >= scene.images.size()) return nullptr;
        if (image_decoded[idx]) return &decoded_images[idx];
        const auto& img = scene.images[idx];
        if (!ImageLoad::load_memory(img.bytes.data(), img.bytes.size(),
                                    img.name, decoded_images[idx], err)) {
            err = "texture '" + img.name + "': " + err;
            return nullptr;
        }
        image_decoded[idx] = true;
        return &decoded_images[idx];
    };
    auto add_texture = [&](const std::string& raw_name,
                           const TexWriter::BuiltTex& built) {
        const std::string name = unique_name(
            used_names, sanitize_name(raw_name));
        const std::string vpath = folder + "\\" + name + ".tex";
        queue_tex(vpath, built, pending);
        res.tex_virtual_paths.push_back(vpath);
        res.notes.push_back("texture " + vpath + " (" +
                            std::to_string(built.width) + "x" +
                            std::to_string(built.height) + ", " +
                            std::to_string(built.mip_count) + " mips)");
        prog = std::min(prog + 4, 55);
        progress_update(prog, 100, "Encoded " + name);
        return vpath;
    };
    auto encode_image = [&](int idx, bool normal,
                            std::string& out_path) -> bool {
        ImageLoad::Image* decoded = decode_image(idx);
        if (!decoded) return false;
        TexWriter::Options topt;
        topt.max_dimension = opt.max_tex_dim;
        topt.generate_mips = opt.generate_mips;
        topt.format = normal ? TexWriter::Format::BC5Normal : opt.tex_format;
        TexWriter::BuiltTex built;
        if (!TexWriter::build_from_rgba(decoded->rgba.data(), decoded->width,
                                        decoded->height, topt, built, err)) {
            err = "texture '" + scene.images[idx].name + "': " + err;
            return false;
        }
        if (!verify_tex(built, err)) return false;
        std::string name = scene.images[idx].name;
        if (normal && generic_images.count(idx)) name += "_normal";
        out_path = add_texture(name, built);
        return true;
    };
    for (int idx : generic_images) {
        if (!encode_image(idx, false, image_vpath[idx])) return false;
    }
    for (int idx : normal_images) {
        if (!encode_image(idx, true, normal_vpath[idx])) return false;
    }

    std::vector<std::string> material_diffuse(scene.materials.size());
    for (size_t mi = 0; mi < scene.materials.size(); ++mi) {
        const auto& mt = scene.materials[mi];
        if (mt.base_color >= 0) {
            material_diffuse[mi] = image_vpath[mt.base_color];
            continue;
        }

        ImageLoad::Image color_image;
        color_image.width = 8;
        color_image.height = 8;
        color_image.rgba.resize(8 * 8 * 4);
        for (int pixel = 0; pixel < 64; ++pixel) {
            for (int channel = 0; channel < 4; ++channel) {
                color_image.rgba[pixel * 4 + channel] = uint8_t(
                    std::lround(mt.base_color_factor[channel] * 255.0f));
            }
        }
        TexWriter::Options topt;
        topt.max_dimension = 8;
        topt.generate_mips = opt.generate_mips;
        topt.format = opt.tex_format;
        TexWriter::BuiltTex built;
        if (!TexWriter::build_from_rgba(
                color_image.rgba.data(), color_image.width,
                color_image.height, topt, built, err)) {
            return false;
        }
        if (!verify_tex(built, err)) return false;
        std::string name = mt.name.empty() ? asset : mt.name;
        name += "_flat";
        material_diffuse[mi] = add_texture(name, built);
    }

    progress_update(60, 100, "Building model");
    std::vector<MdlWriter::MeshInput> meshes;
    for (const auto& prim : scene.prims) {
        MdlWriter::MeshInput m;
        m.name = sanitize_name(prim.name);
        m.positions = prim.positions;
        m.normals = prim.normals;
        m.uvs = prim.uvs;
        m.indices = prim.indices;
        if (prim.material >= 0 &&
            (size_t)prim.material < scene.materials.size()) {
            const auto& mt = scene.materials[prim.material];
            auto pathof = [&](int idx) -> std::string {
                return (idx >= 0 && (size_t)idx < image_vpath.size())
                           ? image_vpath[idx] : std::string();
            };
            m.tex_diffuse = material_diffuse[prim.material];
            m.tex_specular = pathof(mt.occlusion);
            if (mt.normal >= 0 && size_t(mt.normal) < normal_vpath.size())
                m.tex_normal = normal_vpath[mt.normal];
            m.tex_metallic = pathof(mt.metallic_rough);
            m.tex_extra    = pathof(mt.emissive);
        }
        meshes.push_back(std::move(m));
    }

    MdlWriter::BuiltMdl built_mdl;
    if (!MdlWriter::build(meshes, built_mdl, err)) return false;
    if (!verify_mdl(built_mdl, err)) return false;

    const std::string mdl_vpath = folder + "\\" + asset + ".mdl";
    pending.push_back({"globals_model_headers.bnk", mdl_vpath, built_mdl.header});
    pending.push_back({"globals_models.bnk", mdl_vpath, built_mdl.body});

    
    progress_update(75, 100, "Injecting into BNKs");
    std::vector<std::pair<std::string, std::string>> injected;
    if (!inject_entries(pending, injected, err)) return false;

    progress_update(95, 100, "Refreshing indices");
    register_injected(injected);

    res.mdl_virtual_path = mdl_vpath;
    res.meshes = built_mdl.mesh_count;
    res.vertices = built_mdl.vertex_count;
    res.triangles = built_mdl.triangle_count;
    res.notes.push_back("model " + mdl_vpath + " (" +
                        std::to_string(built_mdl.mesh_count) + " meshes, " +
                        std::to_string(built_mdl.vertex_count) + " verts, " +
                        std::to_string(built_mdl.triangle_count) + " tris)");

    if (opt.create_gdb_template) {
        progress_update(98, 100, "Creating spawnable entity");
        create_spawn_template(mdl_vpath, opt, asset, res);
    }
    debug_scope.Result("success | meshes=" +
                       std::to_string(res.meshes) + " | textures=" +
                       std::to_string(res.tex_virtual_paths.size()));
    return true;
}
