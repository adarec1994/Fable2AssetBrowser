std::string PreferredTextureBank()
{
    return (S.selected_nested_index != -1 &&
            !S.selected_nested_temp_path.empty())
        ? S.selected_nested_temp_path
        : S.selected_bnk;
}
