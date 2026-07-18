
static inline std::string tolower_copy(std::string s){ std::transform(s.begin(), s.end(), s.begin(), ::tolower); return s; }
static inline std::string basename_lower_noext(const std::string& s){
    auto b = std::filesystem::path(s).filename().string();
    auto p = b.find_last_of('.');
    if(p!=std::string::npos) b = b.substr(0,p);
    return tolower_copy(b);
}
static bool mp_is_adjacent_terrain_mesh(const MPPerMesh& m)
{
    return m.name.rfind("adjacent terrain", 0) == 0;
}
bool g_mp_vista_only = false;
static bool mp_should_hide_mesh(const MPPerMesh& m)
{
    if (g_mp_vista_only && !mp_is_adjacent_terrain_mesh(m)) return true;
    if (m.is_entity_model && !S.show_entity_models) return true;
    return !S.show_adjacent_terrain && mp_is_adjacent_terrain_mesh(m);
}
static inline std::string force_tex_ext(const std::string& s){
    std::string base = std::filesystem::path(s).filename().string();
    auto p = base.find_last_of('.');
    if(p!=std::string::npos) base = base.substr(0,p);
    return base + ".tex";
}
static std::optional<std::string> find_any_textures_bnk(){
    if(auto p1 = find_bnk_by_filename("globals_textures.bnk"); p1) return p1;
    return find_bnk_by_filename("global_textures.bnk");
}
static inline uint8_t ex5(uint16_t v){ return (uint8_t)((v<<3)|(v>>2)); }
static inline uint8_t ex6(uint16_t v){ return (uint8_t)((v<<2)|(v>>4)); }
