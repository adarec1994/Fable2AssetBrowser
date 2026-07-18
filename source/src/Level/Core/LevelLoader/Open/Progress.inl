static void loader_progress_update(int current,
                                   int total,
                                   const std::string& text)
{
    if (!g_level_export_only_load.load()) {
        progress_update(current, total, text);
    }
}
