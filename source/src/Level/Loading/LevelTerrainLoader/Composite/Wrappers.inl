bool BakeEhfTerrainComposite(const std::vector<uint8_t>& ehf,
                             std::vector<uint8_t>&  out_rgba,
                             int&                   out_w,
                             int&                   out_h,
                             std::string&           out_picked_name)
{
    return BakeEhfTerrainCompositeWithBnk(ehf, {},
                                          out_rgba, out_w, out_h,
                                          out_picked_name);
}

namespace { bool g_capture_splat_output = false;
            std::vector<uint8_t>* g_splat_output_rgba = nullptr;
            int* g_splat_output_w = nullptr;
            int* g_splat_output_h = nullptr; }

bool BakeEhfTerrainCompositeAndSplat(
    const std::vector<uint8_t>& ehf,
    const std::string& preferred_bnk,
    std::vector<uint8_t>& out_rgba,
    int& out_w, int& out_h,
    std::string& out_picked_name,
    std::vector<uint8_t>& out_splat_rgba,
    int& out_splat_w, int& out_splat_h)
{
    g_capture_splat_output = true;
    g_splat_output_rgba    = &out_splat_rgba;
    g_splat_output_w       = &out_splat_w;
    g_splat_output_h       = &out_splat_h;
    bool ok = BakeEhfTerrainCompositeWithBnk(ehf, preferred_bnk,
                                             out_rgba, out_w, out_h,
                                             out_picked_name);
    g_capture_splat_output = false;
    g_splat_output_rgba = nullptr;
    g_splat_output_w = nullptr;
    g_splat_output_h = nullptr;
    return ok;
}
