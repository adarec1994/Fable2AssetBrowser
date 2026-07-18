bool PatchGameflowEligibility(const std::string& source,
                              const std::string& quest_id,
                              const std::string& eligibility_lua,
                              std::string& patched,
                              std::string& error) {
    patched.clear();
    error.clear();
    if (!IsValidQuestId(quest_id) || eligibility_lua.empty()) {
        error = "invalid authored quest eligibility";
        return false;
    }

    const std::string newline =
        source.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::string begin_marker =
        "-- FABLE2_ASSET_BROWSER QUEST " + quest_id + " BEGIN";
    const std::string end_marker =
        "-- FABLE2_ASSET_BROWSER QUEST " + quest_id + " END";

    const std::size_t existing_begin = source.find(begin_marker);
    if (existing_begin != std::string::npos) {
        const std::size_t existing_end =
            source.find(end_marker, existing_begin + begin_marker.size());
        if (existing_end == std::string::npos) {
            error = "existing quest eligibility marker is incomplete";
            return false;
        }
        const std::size_t line_start_pos = source.rfind('\n', existing_begin);
        const std::size_t line_start =
            line_start_pos == std::string::npos ? 0 : line_start_pos + 1;
        const std::size_t non_space =
            source.find_first_not_of(" \t\r", line_start);
        const std::string indent = non_space == std::string::npos
            ? std::string{} : source.substr(line_start, non_space - line_start);
        std::size_t replace_end =
            source.find('\n', existing_end + end_marker.size());
        if (replace_end == std::string::npos) replace_end = source.size();
        else ++replace_end;
        const std::string block =
            indent + begin_marker + newline +
            indent + eligibility_lua + newline +
            indent + end_marker + newline;
        patched = source;
        patched.replace(line_start, replace_end - line_start, block);
        return true;
    }

    const std::size_t update =
        source.find("function GameflowQuestUnlocker:Update()");
    if (update == std::string::npos) {
        error = "GameflowQuestUnlocker:Update was not found";
        return false;
    }
    const std::size_t loop = source.find("while true do", update);
    if (loop == std::string::npos) {
        error = "GameflowQuestUnlocker update loop was not found";
        return false;
    }
    const std::size_t gate =
        source.find("Gameflow.GameflowPositionUpdated", loop);
    if (gate == std::string::npos) {
        error = "gameflow eligibility gate was not found";
        return false;
    }
    const std::size_t gate_line_start_pos = source.rfind('\n', gate);
    const std::size_t gate_line_start = gate_line_start_pos == std::string::npos
        ? 0 : gate_line_start_pos + 1;
    const std::size_t gate_non_space =
        source.find_first_not_of(" \t\r", gate_line_start);
    const std::string gate_indent = gate_non_space == std::string::npos
        ? std::string{}
        : source.substr(gate_line_start, gate_non_space - gate_line_start);
    std::size_t insertion = source.find('\n', gate);
    if (insertion == std::string::npos) insertion = source.size();
    else ++insertion;

    const std::string indent = gate_indent + "\t";
    const std::string block =
        indent + begin_marker + newline +
        indent + eligibility_lua + newline +
        indent + end_marker + newline;
    patched = source;
    patched.insert(insertion, block);
    return true;
}
