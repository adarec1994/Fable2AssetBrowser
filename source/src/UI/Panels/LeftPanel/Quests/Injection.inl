std::atomic<bool> g_quest_injection_busy{false};
void inject_active_authored_quest();

std::string authored_quest_entry_path(const std::string& quest_id) {
    std::string lower = quest_id;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return "scripts/quests/" + lower + ".lua";
}

bool collect_quest_injection_targets(
    const std::string& quest_id,
    std::vector<QuestInjection::BankTarget>& targets,
    std::string& error) {
    targets.clear();
    error.clear();
    const std::filesystem::path data =
        std::filesystem::path(S.root_dir) / "data";
    for (const char* filename : {"gamescripts.bnk",
                                 "gamescripts_r.bnk"}) {
        QuestInjection::BankTarget target;
        if (ISO::IsoMount::instance().is_mounted() &&
            S.root_dir == ISO::IsoMount::instance().iso_path()) {
            const std::string member = std::string("data/") + filename;
            if (!ISO::IsoMount::instance().find(member)) continue;
            target.path = ISO::IsoMount::make_iso_path(member);
        } else {
            const std::filesystem::path path = data / filename;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(path, ec)) continue;
            target.path = path.string();
        }
        target.gameflow_lua_index = BnkCache::find_index(
            target.path, "scripts/quests/gameflow.lua");
        target.gameflow_text_index = BnkCache::find_index(
            target.path, "scripts/quests/gameflow.txt");
        target.quest_script_index = BnkCache::find_index(
            target.path, authored_quest_entry_path(quest_id));
        if (target.gameflow_lua_index < 0 ||
            target.gameflow_text_index < 0) {
            error = std::string(filename) +
                    " does not contain gameflow.lua and gameflow.txt";
            return false;
        }
        try {
            target.gameflow_source = BnkCache::extract_bytes(
                target.path, target.gameflow_text_index);
        } catch (const std::exception& ex) {
            error = std::string("Could not read ") + filename + ": " +
                    ex.what();
            return false;
        }
        targets.push_back(std::move(target));
    }
    if (targets.empty()) {
        error = "No gamescripts.bnk files were found in the selected "
                "Fable 2 source.";
        return false;
    }
    return true;
}

void inject_active_authored_quest() {
    {
        std::string backup_error;
        if (!GameBackup::RequireBackup(backup_error)) {
            OutputLog::error("quest save: " + backup_error);
            return;
        }
    }
    if (g_quest_injection_busy.exchange(true)) return;
    std::string validation_error;
    if (!QuestUI::ValidateActiveAuthoredQuest(validation_error)) {
        g_quest_injection_busy = false;
        show_error_box(validation_error);
        return;
    }
    if (LevelEdit::Dirty()) {
        g_quest_injection_busy = false;
        show_error_box(
            "Save the referenced level first. Its normal level backup "
            "will protect the NPC/container changes.");
        return;
    }
    const std::string quest_id = QuestUI::ActiveAuthoredQuestId();
    const std::string quest_lua = QuestUI::ActiveAuthoredQuestLua();
    const std::string eligibility =
        QuestUI::ActiveAuthoredEligibilityLua();
    const auto localized_text = QuestUI::ActiveAuthoredTextEntries();
    const std::string root = S.root_dir;
    std::vector<QuestInjection::BankTarget> targets;
    std::string error;
    if (!collect_quest_injection_targets(quest_id, targets, error)) {
        g_quest_injection_busy = false;
        show_error_box(error);
        return;
    }

    progress_open(100, "Saving " + quest_id + "...");
    std::thread([root, quest_id, quest_lua, eligibility, localized_text,
                 targets = std::move(targets)]() mutable {
        std::string result;
        std::string error;
        const bool ok = QuestInjection::Inject(
            root, quest_id, quest_lua, eligibility, localized_text, targets,
            result, error);
        for (const QuestInjection::BankTarget& target : targets) {
            BnkCache::invalidate(target.path);
        }
        if (ok) {
            OutputLog::success("quest saved: " + result);
            show_completion_box(result);
        } else {
            OutputLog::error("quest save failed: " + error);
            show_error_box("Quest save failed:\n" + error);
        }
        progress_done();
        g_quest_injection_busy = false;
    }).detach();
}
