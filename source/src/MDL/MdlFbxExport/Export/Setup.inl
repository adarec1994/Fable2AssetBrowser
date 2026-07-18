bool mdl_to_fbx_full(const std::vector<unsigned char>& mdl_data,
                     const std::string& fbx_path,
                     const std::string& mdl_source_path,
                     std::string& err_msg,
                     bool include_animations) {
    err_msg.clear();

    MDLInfo info;
    std::vector<MDLMeshGeom> geoms;
    if (!MdlExport::parse_model_for_export(mdl_data, mdl_source_path,
                                           info, geoms, err_msg)) {
        return false;
    }

    IdGen ids;

    struct BoneOut {
        int orig_idx;
        int filtered_idx;
        int parent_filt;
        std::string name;

        double qx, qy, qz, qw, tx, ty, tz, sx, sy, sz;
        Mat4 local;
        Mat4 world;
        int64_t model_id;
        int64_t attr_id;
        int64_t cluster_id;
        bool used_as_cluster;
    };
    std::vector<BoneOut> bones;
    std::vector<int> orig_to_filt(info.BoneCount, -1);
    for (size_t i = 0; i < info.Bones.size(); ++i) {
        if (info.Bones[i].Name.find("Rig_Asset") != std::string::npos) continue;
        BoneOut b{};
        b.orig_idx = (int)i;
        b.filtered_idx = (int)bones.size();
        b.name = info.Bones[i].Name;
        b.parent_filt = -1;
        if (i < info.BoneTransforms.size() &&
            info.BoneTransforms[i].size() >= 10) {
            const auto& tf = info.BoneTransforms[i];
            b.qx = tf[0]; b.qy = tf[1]; b.qz = tf[2]; b.qw = tf[3];
            b.tx = tf[4]; b.ty = tf[5]; b.tz = tf[6];
            b.sx = tf[7]; b.sy = tf[8]; b.sz = tf[9];
        } else {
            b.qx = b.qy = b.qz = 0; b.qw = 1;
            b.tx = b.ty = b.tz = 0;
            b.sx = b.sy = b.sz = 1;
        }
        b.local = Mat4::from_trs_quat(b.qx, b.qy, b.qz, b.qw,
                                      b.tx, b.ty, b.tz,
                                      b.sx, b.sy, b.sz);
        b.world = Mat4::identity();
        b.model_id = ids.make();
        b.attr_id  = ids.make();
        b.cluster_id = 0;
        b.used_as_cluster = false;
        orig_to_filt[i] = b.filtered_idx;
        bones.push_back(std::move(b));
    }

    for (auto& b : bones) {
        int parent_orig = info.Bones[b.orig_idx].ParentID;
        if (parent_orig >= 0 && parent_orig < (int)orig_to_filt.size()) {
            int pf = orig_to_filt[parent_orig];
            if (pf >= 0 && pf != b.filtered_idx) b.parent_filt = pf;
        }
    }
    for (auto& b : bones) {
        if (b.parent_filt < 0) {
            b.world = b.local;
        } else {
            b.world = Mat4::multiply(bones[b.parent_filt].world, b.local);
        }
    }

    {
        constexpr double kS2 = 0.70710678118654752440;
        for (auto& b : bones) {
            if (b.parent_filt >= 0) continue;

            double old_ty = b.ty, old_tz = b.tz;
            b.ty = -old_tz;
            b.tz =  old_ty;

            double q2x = b.qx, q2y = b.qy, q2z = b.qz, q2w = b.qw;
            b.qx = kS2 * (q2x + q2w);
            b.qy = kS2 * (q2y - q2z);
            b.qz = kS2 * (q2z + q2y);
            b.qw = kS2 * (q2w - q2x);

            b.local = Mat4::from_trs_quat(b.qx, b.qy, b.qz, b.qw,
                                          b.tx, b.ty, b.tz,
                                          b.sx, b.sy, b.sz);
        }

        for (auto& b : bones) {
            if (b.parent_filt < 0) {
                b.world = b.local;
            } else {
                b.world = Mat4::multiply(bones[b.parent_filt].world, b.local);
            }
        }
    }
