static bool extract_tex_bytes_by_candidate(const std::vector<std::string>& candidates, std::vector<unsigned char>& out){
    auto pOpt = find_any_textures_bnk();
    if(!pOpt) return false;
    BNKReader r(*pOpt);
    std::vector<std::string> wanted;
    for(const auto& c : candidates){
        if(c.empty()) continue;
        wanted.push_back(tolower_copy(c));
        std::string fname = std::filesystem::path(c).filename().string();
        wanted.push_back(tolower_copy(fname));
        wanted.push_back(tolower_copy(force_tex_ext(c)));
        wanted.push_back(tolower_copy(force_tex_ext(fname)));
        wanted.push_back(basename_lower_noext(c));
    }
    std::sort(wanted.begin(), wanted.end());
    wanted.erase(std::unique(wanted.begin(), wanted.end()), wanted.end());
    int best_idx = -1;
    size_t best_area = 0;
    for(size_t i=0;i<r.list_files().size();++i){
        const auto& e = r.list_files()[i];
        std::string fn = std::filesystem::path(e.name).filename().string();
        std::string fn_low = tolower_copy(fn);
        std::string fn_base_noext = basename_lower_noext(fn);
        bool match = false;
        for(const auto& w : wanted){
            if(fn_low == w || fn_base_noext == w){ match = true; break; }
        }
        if(!match) continue;
        std::vector<unsigned char> blob;
        try{
            auto dir = std::filesystem::temp_directory_path()/ "f2_tex_pick";
            std::error_code ec; std::filesystem::create_directories(dir, ec);
            auto outp = dir/("tex_"+std::to_string((uint64_t)i)+".bin");
            extract_one(*pOpt, (int)i, outp.string());
            blob = read_all_bytes(outp);
            std::filesystem::remove(outp, ec);
        }catch(...){ continue; }
        if(blob.empty()) continue;
        TexInfo ti{};
        if(!parse_tex_info(blob, ti)) continue;

        if (ti.Mips.empty()) continue;
        size_t area = (size_t)ti.TextureWidth * (size_t)ti.TextureHeight;
        if(area > best_area){ best_area = area; best_idx = (int)i; out.swap(blob); }
    }
    return best_idx >= 0 && !out.empty();
}
