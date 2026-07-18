bool IsExportInProgress()
{
    return g_level_exporting.load();
}

void ExportAsync(const FlatAssetEntry& entry, ExportFormat format)
{
    bool expected = false;
    if (!g_level_exporting.compare_exchange_strong(expected, true)) {
        OutputLog::warn("level export already in progress");
        return;
    }
    S.cancel_requested.store(false);
    progress_open(100, "Exporting level...");
    std::thread([entry, format]() {
        struct Guard {
            ~Guard()
            {
                progress_done();
                S.cancel_requested.store(false);
                g_level_exporting.store(false);
            }
        } guard;

        bool ok = false;
        try {
            ok = run_export(entry, format);
        } catch (const std::exception& ex) {
            OutputLog::error("level export failed: " + std::string(ex.what()));
        } catch (...) {
            OutputLog::error("level export failed: unknown exception");
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn("Level export cancelled.");
        }
    }).detach();
}
