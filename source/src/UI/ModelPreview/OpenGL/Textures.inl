static unsigned int load_tex_from_name(const std::string& name, bool* out_has_alpha) {
    if (name.empty()) return 0;
    std::vector<unsigned char> tex_buf;

    std::string preferred_for_tex =
        (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty())
            ? S.selected_nested_temp_path
            : S.selected_bnk;
    if (build_any_tex_buffer_for_name(name, tex_buf, preferred_for_tex)) {
        std::vector<uint8_t> rgba;
        int w, h;
        if (decode_tex_to_rgba(tex_buf, rgba, w, h, out_has_alpha)) {
            return create_gl_texture_from_rgba(w, h, rgba.data());
        }
    }
    return 0;
}
