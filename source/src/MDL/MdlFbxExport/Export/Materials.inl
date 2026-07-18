    enum TexChannel { CH_DIFFUSE, CH_NORMAL, CH_SPECULAR, CH_METALLIC, CH_EXTRA, CH_COUNT };

    struct EmbeddedTex {
        int64_t  video_id   = 0;
        int64_t  tex_id     = 0;
        std::string name;
        std::string filename;
        std::string mime_ext;
        std::vector<uint8_t> bytes;
        const char* fbx_property = "DiffuseColor";
    };

    struct MeshOut {
        int64_t geom_id;
        int64_t model_id;
        int64_t mat_id;
        int64_t skin_id;
        std::string name;
        EmbeddedTex tex[CH_COUNT];

        std::unordered_map<int, int64_t> cluster_for_bone;
        std::unordered_map<int, std::vector<int>> cluster_indexes;
        std::unordered_map<int, std::vector<double>> cluster_weights;
    };
    std::vector<MeshOut> meshes_out;
    meshes_out.reserve(geoms.size());

    const MdlTexExport::Format tex_fmt =
        MdlTexExport::format_from_string(S.mdl_texture_export_format);

    for (size_t gi = 0; gi < geoms.size(); ++gi) {
        const auto& g = geoms[gi];
        MeshOut mo{};
        mo.geom_id  = ids.make();
        mo.model_id = ids.make();
        mo.mat_id   = ids.make();
        mo.skin_id  = ids.make();
        mo.name     = g.name.empty() ? ("mesh_" + std::to_string(gi)) : g.name;

        struct ChanBinding {
            const std::string& src;
            const char* fbx_prop;
        };
        const ChanBinding bindings[CH_COUNT] = {
            { g.diffuse_tex_name,  "DiffuseColor"    },
            { g.normal_tex_name,   "NormalMap"       },
            { g.specular_tex_name, "SpecularColor"   },
            { g.metallic_tex_name, "ReflectionColor" },
            { g.extra_tex_name,    "EmissiveColor"   },
        };

        for (int ch = 0; ch < CH_COUNT; ++ch) {
            const auto& bind = bindings[ch];
            if (bind.src.empty()) continue;
            std::vector<unsigned char> tex_buf;
            if (!build_any_tex_buffer_for_name(bind.src, tex_buf,
                                               std::string())) {
                continue;
            }
            MdlTexExport::EncodedTex enc;
            if (!MdlTexExport::encode_largest_mip(tex_buf, tex_fmt, enc) ||
                enc.bytes.empty()) {
                continue;
            }
            EmbeddedTex& et = mo.tex[ch];
            et.video_id     = ids.make();
            et.tex_id       = ids.make();
            et.name         = bind.src;
            et.mime_ext     = enc.extension;
            et.filename     = bind.src + et.mime_ext;
            et.bytes        = std::move(enc.bytes);
            et.fbx_property = bind.fbx_prop;
        }

        const size_t vcount = g.positions.size() / 3;
        for (size_t v = 0; v < vcount; ++v) {
            for (int j = 0; j < 4; ++j) {
                if (v * 4 + j >= g.bone_ids.size()) break;
                int orig = g.bone_ids[v * 4 + j];
                float w = (v * 4 + j < g.bone_weights.size())
                              ? g.bone_weights[v * 4 + j] : 0.0f;
                if (w <= 0.0f) continue;
                if (orig < 0 || orig >= (int)orig_to_filt.size()) continue;
                int filt = orig_to_filt[orig];
                if (filt < 0) continue;
                mo.cluster_indexes[filt].push_back((int)v);
                mo.cluster_weights[filt].push_back((double)w);
            }
        }
        for (auto& kv : mo.cluster_indexes) {
            int filt = kv.first;
            int64_t cid = ids.make();
            mo.cluster_for_bone[filt] = cid;
            bones[filt].used_as_cluster = true;
        }

        meshes_out.push_back(std::move(mo));
    }
