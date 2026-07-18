    struct FbxAnimCurve {
        int64_t id = 0;
        std::string axis;
        std::vector<int64_t> times;
        std::vector<float> values;
    };
    struct FbxAnimCurveNode {
        int64_t id = 0;
        int bone_filt = -1;
        std::string property;
        std::vector<FbxAnimCurve> curves;
    };
    struct FbxAnimClip {
        std::string name;
        int64_t stack_id = 0;
        int64_t layer_id = 0;
        int64_t start = 0;
        int64_t stop = 0;
        std::vector<FbxAnimCurveNode> nodes;
    };

    std::vector<FbxAnimClip> fbx_anims;
    if (include_animations) {
        const uint32_t export_bone_count =
            MdlExport::model_bone_count_for_export(info);
        const std::vector<MdlExport::ExportAnimClip> export_anims =
            MdlExport::collect_compatible_animations(
                info, export_bone_count, orig_to_filt, mdl_source_path);

        for (const MdlExport::ExportAnimClip& clip : export_anims) {
            if (clip.times.empty() || clip.channels.empty()) continue;

            FbxAnimClip ac;
            ac.name = fbx_clean_name(clip.name);
            ac.stack_id = ids.make();
            ac.layer_id = ids.make();
            ac.start = fbx_time(clip.times.front());
            ac.stop = fbx_time(clip.times.back());

            for (const MdlExport::ExportAnimChannel& ch : clip.channels) {
                if (ch.target_node < 0 ||
                    ch.target_node >= (int)bones.size()) {
                    continue;
                }

                if (ch.has_rotation && !ch.rotations.empty()) {
                    FbxAnimCurveNode cn;
                    cn.id = ids.make();
                    cn.bone_filt = ch.target_node;
                    cn.property = "Lcl Rotation";
                    cn.curves.resize(3);
                    cn.curves[0].axis = "d|X";
                    cn.curves[1].axis = "d|Y";
                    cn.curves[2].axis = "d|Z";
                    for (FbxAnimCurve& curve : cn.curves) {
                        curve.id = ids.make();
                        curve.times.reserve(clip.times.size());
                        curve.values.reserve(clip.times.size());
                    }
                    const size_t frames =
                        std::min(clip.times.size(), ch.rotations.size() / 4);
                    for (size_t frame = 0; frame < frames; ++frame) {
                        const size_t q = frame * 4;
                        double qx = ch.rotations[q + 0];
                        double qy = ch.rotations[q + 1];
                        double qz = ch.rotations[q + 2];
                        double qw = ch.rotations[q + 3];
                        if (bones[(size_t)ch.target_node].parent_filt < 0) {
                            fbx_root_quat(qx, qy, qz, qw);
                        }
                        double rx = 0.0, ry = 0.0, rz = 0.0;
                        quat_to_euler_deg(qx, qy, qz, qw, rx, ry, rz);
                        const int64_t kt = fbx_time(clip.times[frame]);
                        cn.curves[0].times.push_back(kt);
                        cn.curves[1].times.push_back(kt);
                        cn.curves[2].times.push_back(kt);
                        cn.curves[0].values.push_back((float)rx);
                        cn.curves[1].values.push_back((float)ry);
                        cn.curves[2].values.push_back((float)rz);
                    }
                    ac.nodes.push_back(std::move(cn));
                }

                if (ch.has_translation && !ch.translations.empty()) {
                    FbxAnimCurveNode cn;
                    cn.id = ids.make();
                    cn.bone_filt = ch.target_node;
                    cn.property = "Lcl Translation";
                    cn.curves.resize(3);
                    cn.curves[0].axis = "d|X";
                    cn.curves[1].axis = "d|Y";
                    cn.curves[2].axis = "d|Z";
                    for (FbxAnimCurve& curve : cn.curves) {
                        curve.id = ids.make();
                        curve.times.reserve(clip.times.size());
                        curve.values.reserve(clip.times.size());
                    }
                    const size_t frames =
                        std::min(clip.times.size(), ch.translations.size() / 3);
                    for (size_t frame = 0; frame < frames; ++frame) {
                        const size_t t = frame * 3;
                        double tx = ch.translations[t + 0];
                        double ty = ch.translations[t + 1];
                        double tz = ch.translations[t + 2];
                        if (bones[(size_t)ch.target_node].parent_filt < 0) {
                            const double old_ty = ty;
                            const double old_tz = tz;
                            ty = -old_tz;
                            tz =  old_ty;
                        }
                        const int64_t kt = fbx_time(clip.times[frame]);
                        cn.curves[0].times.push_back(kt);
                        cn.curves[1].times.push_back(kt);
                        cn.curves[2].times.push_back(kt);
                        cn.curves[0].values.push_back((float)tx);
                        cn.curves[1].values.push_back((float)ty);
                        cn.curves[2].values.push_back((float)tz);
                    }
                    ac.nodes.push_back(std::move(cn));
                }
            }

            if (!ac.nodes.empty()) {
                fbx_anims.push_back(std::move(ac));
            }
        }
    }
