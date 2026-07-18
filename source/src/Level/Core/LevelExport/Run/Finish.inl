    const auto fable_path = out_dir / (folder_name + ".fable");
    std::string json_err;
    if (!write_text(fable_path, js.str(), json_err)) {
        OutputLog::error("level export: .fable write failed: " + json_err);
        return false;
    }

    progress_update(100, 100, "Done");
    std::ostringstream msg;
    msg << "Level exported: " << folder_name << " (" << model_files.size()
        << " model(s), " << instance_written << " placement(s)";
    if (reused_models > 0) msg << ", " << reused_models << " reused";
    if (exported_models > 0) msg << ", " << exported_models << " written";
    if (failed_models > 0) msg << ", " << failed_models << " model miss(es)";
    msg << ") -> " << out_dir.string();
    OutputLog::success(msg.str());
    return true;
}
