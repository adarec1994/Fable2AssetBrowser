bool level_edit_click_guard(const char* what) {
    if (!LevelEdit::Enabled() && !LevelEdit::Dirty() &&
        !LevelEdit::Saving()) {
        return false;
    }
    const std::string msg =
        std::string(what) + " is disabled while a level edit is active";
    if (!S.level_edit_guard_seen) {
        S.level_edit_guard_seen = true;
        S.level_edit_guard_message = msg;
        S.level_edit_guard_popup = true;
    } else {
        OutputLog::warn(msg);
    }
    return true;
}
