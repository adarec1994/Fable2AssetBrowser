    progress_update(93, 100, "Writing .fable...");
    std::ostringstream js;
    js << std::setprecision(9);
    js << "{\n";
    js << "  \"version\": 1,\n";
    js << "  \"format\": \"" << fmt_label << "\",\n";
    js << "  \"level\": {\n";
    js << "    \"name\": \"" << json_escape(folder_name) << "\",\n";
    js << "    \"source\": \"" << json_escape(to_slash(entry.full_path)) << "\"\n";
    js << "  },\n";
    js << "  \"models\": [\n";
    bool first_model = true;
    for (const auto& kv : model_files) {
        if (!first_model) js << ",\n";
        first_model = false;
        js << "    {\"source\":\"" << json_escape(to_slash(kv.first))
           << "\",\"file\":\"" << json_escape(kv.second)
           << "\",\"instances\":" << instance_counts[kv.first] << "}";
    }
    js << "\n  ],\n";
    js << "  \"instances\": [\n";
    bool first_inst = true;
    size_t instance_written = 0;
    for (const auto& block : prop_blocks) {
        auto mf = model_files.find(block.model_path);
        if (mf == model_files.end()) continue;
        for (size_t ii = 0; ii < block.instances.size(); ++ii) {
            const auto& inst = block.instances[ii];
            const auto mat = instance_matrix(inst);
            if (!first_inst) js << ",\n";
            first_inst = false;
            js << "    {\"model\":\"" << json_escape(mf->second)
               << "\",\"source\":\"" << json_escape(to_slash(block.model_path))
               << "\",\"type\":" << block.type
               << ",\"hash\":" << inst.hash
               << ",\"position\":[" << inst.values[0] << ","
               << inst.values[2] << "," << inst.values[1] << "]"
               << ",\"matrix\":";
            write_matrix_json(js, mat);
            js << ",\"fullTransform\":"
               << (inst.has_full_transform ? "true" : "false")
               << ",\"flags\":[" << int(inst.flags[0]) << ","
               << int(inst.flags[1]) << "," << int(inst.flags[2])
               << "],\"raw\":[";
            for (int vi = 0; vi < 20; ++vi) {
                if (vi) js << ",";
                js << inst.values[vi];
            }
            js << "]}";
            ++instance_written;
        }
    }
    js << "\n  ],\n";
