#define IMGUI_DEFINE_MATH_OPERATORS
#include <windows.h>
#include <shobjidl.h>
#include <d3d11.h>
#include <dxgi.h>
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "ImGuiFileDialog.h"
#include "imgui_hex.h"
#include "BNKCore.cpp"
#include "audio.cpp"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <filesystem>
#include <algorithm>
#include <optional>
#include <set>
#include <condition_variable>
#include <chrono>
#include "../resource.h"
#include <unordered_map>
#include <fstream>

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_ID3D11Texture2D, (void**)&pBackBuffer);
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    if (pBackBuffer) pBackBuffer->Release();
}
static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}
static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL levels[1] = { D3D_FEATURE_LEVEL_11_0 };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK) return false;
    CreateRenderTarget();
    return true;
}
static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return 1;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
}

struct BNKItemUI { int index; std::string name; uint32_t size; };

struct TexInfo {
    uint32_t Sign;
    uint32_t RawDataSize;
    uint32_t Unknown_0;
    uint32_t Unknown_1;
    uint32_t TextureWidth;
    uint32_t TextureHeight;
    uint32_t PixelFormat;
    uint32_t MipMap;
    std::vector<uint32_t> MipMapOffset;
    struct MipDef {
        size_t DefOffset;
        uint32_t CompFlag;
        uint32_t DataOffset;
        uint32_t DataSize;
        uint32_t Unknown_3;
        uint32_t Unknown_4;
        uint32_t Unknown_5;
        uint32_t Unknown_6;
        uint32_t Unknown_7;
        uint32_t Unknown_8;
        uint32_t Unknown_9;
        uint32_t Unknown_10;
        uint32_t Unknown_11;
        bool HasWH;
        uint16_t MipWidth;
        uint16_t MipHeight;
        size_t MipDataOffset;
        size_t MipDataSizeParsed;
    };
    std::vector<MipDef> Mips;
};

struct State {
    std::string root_dir;
    std::vector<std::string> bnk_paths;
    std::string bnk_filter;
    std::string selected_bnk;
    std::vector<BNKItemUI> files;
    int selected_file_index = -1;
    bool hide_tooltips = false;
    std::atomic<bool> cancel_requested{false};
    std::atomic<bool> exiting{false};
    std::mutex progress_mutex;
    int progress_total = 0;
    int progress_current = 0;
    std::string progress_label;
    std::atomic<bool> show_progress{false};
    bool show_error = false;
    std::string error_text;
    bool show_completion = false;
    std::string completion_text;
    std::string file_filter;
    std::string last_dir;
    std::atomic<bool> hex_loading{false};
    bool hex_open = false;
    std::string hex_title;
    std::vector<unsigned char> hex_data;
    ImGuiHexEditorState hex_state;
    bool tex_info_ok = false;
    TexInfo tex_info;
    size_t pending_goto = (size_t)-1;
    bool show_preview_popup = false;
    int preview_mip_index = -1;
    ID3D11ShaderResourceView* preview_srv = nullptr;
} S;

static bool is_audio_file(const std::string& n){ std::string s=n; std::transform(s.begin(),s.end(),s.begin(),::tolower); return s.size()>=4 && s.rfind(".wav")==s.size()-4; }
static bool is_tex_file(const std::string& n){ std::string s=n; std::transform(s.begin(),s.end(),s.begin(),::tolower); return s.size()>=4 && s.rfind(".tex")==s.size()-4; }
static bool is_mdl_file(const std::string& n){ std::string s=n; std::transform(s.begin(),s.end(),s.begin(),::tolower); return s.size()>=4 && s.rfind(".mdl")==s.size()-4; }

static std::vector<std::string> filtered_bnk_paths(){
    if(S.bnk_filter.empty()) return S.bnk_paths;
    std::vector<std::string> out; out.reserve(S.bnk_paths.size());
    std::string q=S.bnk_filter; std::transform(q.begin(),q.end(),q.begin(),::tolower);
    for(auto& p: S.bnk_paths){ auto b=std::filesystem::path(p).filename().string(); std::string t=b; std::transform(t.begin(),t.end(),t.begin(),::tolower); if(t.find(q)!=std::string::npos) out.push_back(p); }
    return out;
}

static void progress_open(int total, const std::string& title){
    std::lock_guard<std::mutex> lk(S.progress_mutex);
    S.cancel_requested=false;
    S.progress_total=total;
    S.progress_current=0;
    S.progress_label=title;
    S.show_progress.store(true);
}
static void progress_update(int current, int total, const std::string& fname){
    std::lock_guard<std::mutex> lk(S.progress_mutex);
    S.progress_current=current;
    S.progress_total=total;
    S.progress_label=fname;
}
static void progress_done(){
    std::lock_guard<std::mutex> lk(S.progress_mutex);
    S.show_progress.store(false);
    S.progress_total=0;
    S.progress_current=0;
    S.progress_label.clear();
}
static void show_error_box(const std::string& msg){ S.error_text=msg; S.show_error=true; }
static void show_completion_box(const std::string& msg){ S.completion_text=msg; S.show_completion=true; }
static void refresh_file_table(){ S.selected_file_index=-1; }

static void pick_bnk(const std::string& path){
    S.selected_bnk=path;
    S.files.clear();
    S.file_filter.clear();
    BNKReader reader(path);
    const auto& fe = reader.list_files();
    S.files.reserve(fe.size());
    for(size_t i=0;i<fe.size();++i) S.files.push_back({(int)i, fe[i].name, fe[i].uncompressed_size});
    std::sort(S.files.begin(), S.files.end(), [](const BNKItemUI&a,const BNKItemUI&b){ std::string x=a.name,y=b.name; std::transform(x.begin(),x.end(),x.begin(),::tolower); std::transform(y.begin(),y.end(),y.begin(),::tolower); return x<y; });
    refresh_file_table();
}

static void extract_file_one(const std::string& bnk_path, const BNKItemUI& item, const std::string& base_out_dir, bool convert_audio=true){
    std::filesystem::create_directories(base_out_dir);
    auto dst = std::filesystem::path(base_out_dir) / item.name;
    std::filesystem::create_directories(dst.parent_path());
    extract_one(bnk_path, item.index, dst.string());
    if(convert_audio && is_audio_file(item.name)) convert_wav_inplace_same_name(dst);
}

static void on_extract_selected_raw(){
    int idx=S.selected_file_index;
    if(idx<0 || idx>=(int)S.files.size()){ show_error_box("No file selected."); return; }
    if(S.selected_bnk.empty()){ show_error_box("No BNK selected."); return; }
    auto item=S.files[(size_t)idx];
    auto base_out = (std::filesystem::current_path() / "extracted").string();
    progress_open(1,"Extracting File...");
    progress_update(0,1,item.name);
    std::thread([item,base_out](){
        if(!S.cancel_requested && !S.exiting){ try{ extract_file_one(S.selected_bnk,item,base_out,false); }catch(...){} }
        progress_update(1,1,item.name);
        progress_done();
        if(!S.cancel_requested) show_completion_box(std::string("Extraction complete.\n\nOutput folder:\n")+std::filesystem::absolute(base_out).string());
        S.cancel_requested=false;
    }).detach();
}

static void on_extract_selected_wav(){
    int idx=S.selected_file_index;
    if(idx<0 || idx>=(int)S.files.size()){ show_error_box("No file selected."); return; }
    if(S.selected_bnk.empty()){ show_error_box("No BNK selected."); return; }
    auto item=S.files[(size_t)idx];
    if(!is_audio_file(item.name)){ show_error_box("Selected file is not .wav"); return; }
    auto base_out = (std::filesystem::current_path() / "extracted").string();
    progress_open(1,"Exporting WAV...");
    progress_update(0,1,item.name);
    std::thread([item,base_out](){
        if(!S.cancel_requested && !S.exiting){ try{ extract_file_one(S.selected_bnk,item,base_out,true); }catch(...){} }
        progress_update(1,1,item.name);
        progress_done();
        if(!S.cancel_requested) show_completion_box(std::string("WAV export complete.\n\nOutput folder:\n")+std::filesystem::absolute(base_out).string());
        S.cancel_requested=false;
    }).detach();
}

static void on_dump_all_raw(){
    if(S.selected_bnk.empty()){ show_error_box("No BNK selected."); return; }
    if(S.files.empty()){ show_error_box("No files to dump in this BNK."); return; }
    auto base_out = (std::filesystem::current_path() / "extracted").string();
    int total=(int)S.files.size();
    progress_open(total,"Dumping...");
    progress_update(0,total,"Starting...");
    std::thread([base_out,total](){
        std::atomic<int> dumped{0};
        std::mutex fail_m;
        std::vector<std::string> failed;
        auto work = [&](const BNKItemUI& it){
            if(S.cancel_requested || S.exiting) return;
            try{ extract_file_one(S.selected_bnk,it,base_out,false); }catch(...){ std::lock_guard<std::mutex> lk(fail_m); failed.push_back(it.name); }
            int cur = ++dumped;
            progress_update(cur,total,std::filesystem::path(it.name).filename().string());
        };
        if(!S.cancel_requested){
            std::vector<std::thread> pool;
            int n = std::min(8, std::max(1,(int)std::thread::hardware_concurrency()));
            std::atomic<size_t> i{0};
            for(int t=0;t<n;++t) pool.emplace_back([&](){ for(;;){ size_t k=i.fetch_add(1); if(k>=S.files.size()) break; work(S.files[k]);} });
            for(auto& th: pool) th.join();
        }
        progress_done();
        std::string msg = std::string("Dump complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).string();
        if(!failed.empty()){
            msg += std::string("\nFailed: ") + std::to_string((int)failed.size());
        }
        show_completion_box(msg);
        S.cancel_requested=false;
    }).detach();
}

static void on_export_wavs(){
    if(S.selected_bnk.empty()){ show_error_box("No BNK selected."); return; }
    std::vector<BNKItemUI> audio_files;
    for(auto& f: S.files) if(is_audio_file(f.name)) audio_files.push_back(f);
    if(audio_files.empty()){ show_error_box("No .wav files in this BNK."); return; }
    auto base_out = (std::filesystem::current_path() / "extracted").string();
    int total=(int)audio_files.size();
    progress_open(total,"Exporting WAVs...");
    progress_update(0,total,"Starting...");
    std::thread([audio_files,base_out,total](){
        std::atomic<int> done{0};
        std::mutex fail_m;
        std::vector<std::string> failed;
        auto work = [&](const BNKItemUI& it){
            if(S.cancel_requested || S.exiting) return;
            try{ extract_file_one(S.selected_bnk,it,base_out,true); }catch(...){ std::lock_guard<std::mutex> lk(fail_m); failed.push_back(it.name); }
            int cur = ++done;
            progress_update(cur,total,std::filesystem::path(it.name).filename().string());
        };
        if(!S.cancel_requested){
            std::vector<std::thread> pool;
            int n = std::min(4, std::max(1,(int)std::thread::hardware_concurrency()/2));
            std::atomic<size_t> i{0};
            for(int t=0;t<n;++t) pool.emplace_back([&](){ for(;;){ size_t k=i.fetch_add(1); if(k>=audio_files.size()) break; work(audio_files[k]);} });
            for(auto& th: pool) th.join();
        }
        progress_done();
        std::string msg = std::string("WAV export complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).string();
        if(!failed.empty()){
            msg += std::string("\nFailed: ") + std::to_string((int)failed.size());
        }
        show_completion_box(msg);
        S.cancel_requested=false;
    }).detach();
}

static std::string load_last_dir(){
    std::ifstream f("last_dir.txt");
    std::string s;
    if(f) std::getline(f,s);
    return s;
}
static void save_last_dir(const std::string& p){
    std::ofstream f("last_dir.txt", std::ios::trunc);
    if(f) f<<p;
}

static std::vector<std::string> scan_bnks_recursive(const std::string& root){
    std::vector<std::string> out;
    std::error_code ec;
    for(auto it = std::filesystem::recursive_directory_iterator(root, std::filesystem::directory_options::skip_permission_denied, ec);
        it!=std::filesystem::recursive_directory_iterator(); ++it){
        if(it->is_regular_file(ec)){
            auto p = it->path();
            auto ext = p.extension().string();
            std::transform(ext.begin(),ext.end(),ext.begin(),::tolower);
            if(ext==".bnk") out.push_back(p.string());
        }
    }
    return out;
}

static std::optional<std::string> find_bnk_by_filename(const std::string& fname_lower){
    for(auto& p: S.bnk_paths){
        std::string b = std::filesystem::path(p).filename().string();
        std::transform(b.begin(),b.end(),b.begin(),::tolower);
        if(b==fname_lower) return p;
    }
    return std::nullopt;
}

static void open_folder_logic(const std::string& sel){
    if(sel.empty()){ show_error_box("No folder selected"); return; }
    if(!std::filesystem::exists(sel)){ show_error_box(std::string("Folder does not exist: ")+sel); return; }
    if(!std::filesystem::is_directory(sel)){ show_error_box(std::string("Selected path is not a directory: ")+sel); return; }
    S.root_dir=sel;
    S.last_dir=sel;
    save_last_dir(sel);
    try{
        S.bnk_paths = scan_bnks_recursive(sel);
        if(S.bnk_paths.empty()) S.bnk_paths = find_bnks(sel);
    }catch(...){
        show_error_box("Error searching for BNK files");
        return;
    }
    if(S.bnk_paths.empty()){
        show_error_box(std::string("No .bnk files found in:\n")+sel+std::string("\n\nPlease select a folder containing Fable 2 BNK files."));
        return;
    }
    std::sort(S.bnk_paths.begin(), S.bnk_paths.end(), [](const std::string&a,const std::string&b){
        std::string A=std::filesystem::path(a).filename().string(), B=std::filesystem::path(b).filename().string();
        std::transform(A.begin(),A.end(),A.begin(),::tolower); std::transform(B.begin(),B.end(),B.begin(),::tolower); return A<B;
    });
    S.selected_bnk.clear();
    S.files.clear();
    refresh_file_table();
}

static void on_rebuild_and_extract(){
    auto p_headers = find_bnk_by_filename("globals_texture_headers.bnk");
    auto p_mip0    = find_bnk_by_filename("1024mip0_textures.bnk");
    auto p_rest    = find_bnk_by_filename("globals_textures.bnk");
    if(!p_headers || !p_rest){ show_error_box("Required BNKs not found."); return; }

    BNKReader r_headers(*p_headers);
    BNKReader r_rest(*p_rest);
    std::optional<BNKReader> r_mip0;
    if(p_mip0) r_mip0.emplace(*p_mip0);

    struct Entry { int idx; std::string name; uint32_t size; };
    std::vector<Entry> H, R, M;
    for(size_t i=0;i<r_headers.list_files().size();++i){ auto& e=r_headers.list_files()[i]; H.push_back({(int)i, e.name, e.uncompressed_size}); }
    for(size_t i=0;i<r_rest.list_files().size();++i){ auto& e=r_rest.list_files()[i]; R.push_back({(int)i, e.name, e.uncompressed_size}); }
    if(r_mip0) for(size_t i=0;i<r_mip0->list_files().size();++i){ auto& e=r_mip0->list_files()[i]; M.push_back({(int)i, e.name, e.uncompressed_size}); }

    std::unordered_map<std::string,int> mapH, mapR, mapM;
    mapH.reserve(H.size()*2+1); mapR.reserve(R.size()*2+1); mapM.reserve(M.size()*2+1);
    for(auto& e: H){ std::string k=e.name; std::transform(k.begin(),k.end(),k.begin(),::tolower); mapH.emplace(k,e.idx); }
    for(auto& e: R){ std::string k=e.name; std::transform(k.begin(),k.end(),k.begin(),::tolower); mapR.emplace(k,e.idx); }
    for(auto& e: M){ std::string k=e.name; std::transform(k.begin(),k.end(),k.begin(),::tolower); mapM.emplace(k,e.idx); }

    std::vector<std::string> names;
    names.reserve(std::max(H.size(),R.size()));
    for(auto& e: H) names.push_back(e.name);
    for(auto& e: R){ std::string k=e.name; std::transform(k.begin(),k.end(),k.begin(),::tolower); if(!mapH.count(k)) names.push_back(e.name); }

    int total=(int)names.size();
    if(total<=0){ show_error_box("No texture names found."); return; }

    auto out_root = (std::filesystem::current_path() / "extracted").string();
    progress_open(total,"Rebuilding...");
    progress_update(0,total,"Starting...");
    std::thread([=](){
        int done=0;
        auto tmpdir = std::filesystem::temp_directory_path() / "f2_tex_rebuild";
        std::error_code ec; std::filesystem::create_directories(tmpdir, ec);
        for(auto& name : names){
            if(S.cancel_requested || S.exiting) break;
            std::string key=name; std::transform(key.begin(),key.end(),key.begin(),::tolower);
            if(!mapH.count(key) || !mapR.count(key)){ progress_update(++done,total,name); continue; }
            auto out_path = std::filesystem::path(out_root) / name;
            std::filesystem::create_directories(out_path.parent_path(), ec);

            auto tmp_h   = tmpdir / ("h_"+std::to_string(done)+".bin");
            auto tmp_m   = tmpdir / ("m_"+std::to_string(done)+".bin");
            auto tmp_r   = tmpdir / ("r_"+std::to_string(done)+".bin");

            try{
                extract_one(*p_headers, mapH.at(key), tmp_h.string());
                if(mapM.count(key) && p_mip0) extract_one(*p_mip0, mapM.at(key), tmp_m.string());
                extract_one(*p_rest,    mapR.at(key), tmp_r.string());

                std::ofstream out(out_path, std::ios::binary);
                std::ifstream fh(tmp_h, std::ios::binary);
                out << fh.rdbuf();
                if(std::filesystem::exists(tmp_m)) { std::ifstream fm(tmp_m, std::ios::binary); out << fm.rdbuf(); }
                std::ifstream fr(tmp_r, std::ios::binary);
                out << fr.rdbuf();

                std::filesystem::remove(tmp_h, ec);
                if(std::filesystem::exists(tmp_m)) std::filesystem::remove(tmp_m, ec);
                std::filesystem::remove(tmp_r, ec);
            }catch(...){}
            progress_update(++done,total,name);
        }
        progress_done();
        if(!S.cancel_requested) show_completion_box(std::string("Rebuild complete.\n\nOutput folder:\n")+std::filesystem::absolute(out_root).string());
        S.cancel_requested=false;
    }).detach();
}

static void on_rebuild_and_extract_one(const std::string& tex_name){
    auto p_headers = find_bnk_by_filename("globals_texture_headers.bnk");
    auto p_mip0    = find_bnk_by_filename("1024mip0_textures.bnk");
    auto p_rest    = find_bnk_by_filename("globals_textures.bnk");
    if(!p_headers || !p_rest){ show_error_box("Required BNKs not found."); return; }

    BNKReader r_headers(*p_headers);
    BNKReader r_rest(*p_rest);
    std::optional<BNKReader> r_mip0;
    if(p_mip0) r_mip0.emplace(*p_mip0);

    std::unordered_map<std::string,int> mapH, mapR, mapM;
    for(size_t i=0;i<r_headers.list_files().size();++i){ auto& e=r_headers.list_files()[i]; std::string k=e.name; std::transform(k.begin(),k.end(),k.begin(),::tolower); mapH.emplace(k,(int)i); }
    for(size_t i=0;i<r_rest.list_files().size();++i){ auto& e=r_rest.list_files()[i]; std::string k=e.name; std::transform(k.begin(),k.end(),k.begin(),::tolower); mapR.emplace(k,(int)i); }
    if(r_mip0) for(size_t i=0;i<r_mip0->list_files().size();++i){ auto& e=r_mip0->list_files()[i]; std::string k=e.name; std::transform(k.begin(),k.end(),k.begin(),::tolower); mapM.emplace(k,(int)i); }

    std::string key=tex_name; std::transform(key.begin(),key.end(),key.begin(),::tolower);
    if(!mapH.count(key) || !mapR.count(key)){ show_error_box("Texture not found in required BNKs."); return; }

    auto out_root = (std::filesystem::current_path() / "extracted").string();
    progress_open(1,"Rebuilding...");
    progress_update(0,1,tex_name);
    std::thread([=](){
        auto tmpdir = std::filesystem::temp_directory_path() / "f2_tex_rebuild_one";
        std::error_code ec; std::filesystem::create_directories(tmpdir, ec);
        auto out_path = std::filesystem::path(out_root) / tex_name;
        std::filesystem::create_directories(out_path.parent_path(), ec);
        auto tmp_h = tmpdir / "h.bin";
        auto tmp_m = tmpdir / "m.bin";
        auto tmp_r = tmpdir / "r.bin";
        try{
            extract_one(*p_headers, mapH.at(key), tmp_h.string());
            if(mapM.count(key) && p_mip0) extract_one(*p_mip0, mapM.at(key), tmp_m.string());
            extract_one(*p_rest, mapR.at(key), tmp_r.string());
            std::ofstream out(out_path, std::ios::binary);
            std::ifstream fh(tmp_h, std::ios::binary);
            out << fh.rdbuf();
            if(std::filesystem::exists(tmp_m)) { std::ifstream fm(tmp_m, std::ios::binary); out << fm.rdbuf(); }
            std::ifstream fr(tmp_r, std::ios::binary);
            out << fr.rdbuf();
            std::filesystem::remove(tmp_h, ec);
            if(std::filesystem::exists(tmp_m)) std::filesystem::remove(tmp_m, ec);
            std::filesystem::remove(tmp_r, ec);
        }catch(...){}
        progress_update(1,1,tex_name);
        progress_done();
        if(!S.cancel_requested) show_completion_box(std::string("Rebuild complete.\n\nOutput folder:\n")+std::filesystem::absolute(out_root).string());
        S.cancel_requested=false;
    }).detach();
}

static std::vector<unsigned char> read_all_bytes(const std::filesystem::path& p){
    std::vector<unsigned char> v;
    std::error_code ec;
    auto sz = std::filesystem::file_size(p, ec);
    if(ec) return v;
    v.resize((size_t)sz);
    std::ifstream f(p, std::ios::binary);
    f.read((char*)v.data(), (std::streamsize)sz);
    return v;
}

static bool rd32be(const std::vector<unsigned char>& d, size_t o, uint32_t& v){
    if(o+4>d.size()) return false;
    v = (uint32_t(d[o])<<24)|(uint32_t(d[o+1])<<16)|(uint32_t(d[o+2])<<8)|uint32_t(d[o+3]);
    return true;
}
static bool rd16be(const std::vector<unsigned char>& d, size_t o, uint16_t& v){
    if(o+2>d.size()) return false;
    v = (uint16_t(d[o])<<8)|uint16_t(d[o+1]);
    return true;
}
static bool parse_tex_info(const std::vector<unsigned char>& d, TexInfo& out){
    size_t off=0;
    if(!rd32be(d,off,out.Sign)) return false; off+=4;
    if(!rd32be(d,off,out.RawDataSize)) return false; off+=4;
    if(!rd32be(d,off,out.Unknown_0)) return false; off+=4;
    if(!rd32be(d,off,out.Unknown_1)) return false; off+=4;
    if(!rd32be(d,off,out.TextureWidth)) return false; off+=4;
    if(!rd32be(d,off,out.TextureHeight)) return false; off+=4;
    if(!rd32be(d,off,out.PixelFormat)) return false; off+=4;
    if(!rd32be(d,off,out.MipMap)) return false; off+=4;
    out.MipMapOffset.clear();
    out.Mips.clear();
    if(out.MipMap>0x10000) return false;
    out.MipMapOffset.resize(out.MipMap);
    for(uint32_t i=0;i<out.MipMap;i++){
        if(!rd32be(d,off,out.MipMapOffset[i])) return false;
        off+=4;
    }
    for(uint32_t i=0;i<out.MipMap;i++){
        size_t mo = out.MipMapOffset[i];
        if(mo >= d.size() || mo+4*12>d.size()) return false;
        TexInfo::MipDef md{};
        md.DefOffset = mo;
        size_t k=mo;
        if(!rd32be(d,k,md.CompFlag)) return false; k+=4;
        if(!rd32be(d,k,md.DataOffset)) return false; k+=4;
        if(!rd32be(d,k,md.DataSize)) return false; k+=4;
        if(!rd32be(d,k,md.Unknown_3)) return false; k+=4;
        if(!rd32be(d,k,md.Unknown_4)) return false; k+=4;
        if(!rd32be(d,k,md.Unknown_5)) return false; k+=4;
        if(!rd32be(d,k,md.Unknown_6)) return false; k+=4;
        if(!rd32be(d,k,md.Unknown_7)) return false; k+=4;
        if(!rd32be(d,k,md.Unknown_8)) return false; k+=4;
        if(!rd32be(d,k,md.Unknown_9)) return false; k+=4;
        if(!rd32be(d,k,md.Unknown_10)) return false; k+=4;
        if(!rd32be(d,k,md.Unknown_11)) return false; k+=4;
        md.HasWH=false;
        md.MipWidth=0;
        md.MipHeight=0;
        md.MipDataOffset=0;
        md.MipDataSizeParsed=0;
        if(md.CompFlag==7){
            if(md.DataSize > d.size() || k + md.DataSize > d.size()) return false;
            md.MipDataOffset = k;
            md.MipDataSizeParsed = md.DataSize;
        }else{
            if(k+4+440>d.size()) return false;
            if(!rd16be(d,k,md.MipWidth)) return false; k+=2;
            if(!rd16be(d,k,md.MipHeight)) return false; k+=2;
            k+=440;
            md.HasWH=true;
            if(md.DataSize<448) return false;
            size_t data_sz = md.DataSize-448;
            if(data_sz > d.size() || k + data_sz > d.size()) return false;
            md.MipDataOffset = k;
            md.MipDataSizeParsed = data_sz;
        }
        out.Mips.push_back(md);
    }
    return true;
}

static bool build_tex_buffer_for_name(const std::string& tex_name, std::vector<unsigned char>& out){
    auto p_headers = find_bnk_by_filename("globals_texture_headers.bnk");
    auto p_rest    = find_bnk_by_filename("globals_textures.bnk");
    if(!p_headers || !p_rest) return false;

    BNKReader r_headers(*p_headers);
    BNKReader r_rest(*p_rest);

    auto p_mip0 = find_bnk_by_filename("1024mip0_textures.bnk");
    std::optional<BNKReader> r_mip0;
    if(p_mip0) r_mip0.emplace(*p_mip0);

    std::unordered_map<std::string,int> mapH, mapR, mapM;
    for(size_t i=0;i<r_headers.list_files().size();++i){
        auto& e=r_headers.list_files()[i];
        std::string k=e.name;
        std::transform(k.begin(),k.end(),k.begin(),::tolower);
        mapH.emplace(k,(int)i);
    }
    for(size_t i=0;i<r_rest.list_files().size();++i){
        auto& e=r_rest.list_files()[i];
        std::string k=e.name;
        std::transform(k.begin(),k.end(),k.begin(),::tolower);
        mapR.emplace(k,(int)i);
    }
    if(r_mip0){
        for(size_t i=0;i<r_mip0->list_files().size();++i){
            auto& e=r_mip0->list_files()[i];
            std::string k=e.name;
            std::transform(k.begin(),k.end(),k.begin(),::tolower);
            mapM.emplace(k,(int)i);
        }
    }

    std::string key=tex_name;
    std::transform(key.begin(),key.end(),key.begin(),::tolower);

    if(!mapH.count(key)) return false;

    auto tmpdir = std::filesystem::temp_directory_path() / "f2_tex_hex";
    std::error_code ec;
    std::filesystem::create_directories(tmpdir, ec);

    auto tmp_h = tmpdir / ("h_" + std::to_string(std::hash<std::string>{}(tex_name)) + ".bin");
    auto tmp_m = tmpdir / ("m_" + std::to_string(std::hash<std::string>{}(tex_name)) + ".bin");
    auto tmp_r = tmpdir / ("r_" + std::to_string(std::hash<std::string>{}(tex_name)) + ".bin");

    try{
        extract_one(*p_headers, mapH.at(key), tmp_h.string());

        bool has_mip0 = false;
        if(mapM.count(key) && p_mip0) {
            extract_one(*p_mip0, mapM.at(key), tmp_m.string());
            has_mip0 = std::filesystem::exists(tmp_m);
        }

        bool has_rest = false;
        if(mapR.count(key)) {
            extract_one(*p_rest, mapR.at(key), tmp_r.string());
            has_rest = std::filesystem::exists(tmp_r);
        }

        auto vh = read_all_bytes(tmp_h);
        if(vh.empty()){
            std::filesystem::remove(tmp_h, ec);
            return false;
        }

        std::vector<unsigned char> vm;
        if(has_mip0) {
            vm = read_all_bytes(tmp_m);
        }

        std::vector<unsigned char> vr;
        if(has_rest) {
            vr = read_all_bytes(tmp_r);
        }

        out.clear();
        out.reserve(vh.size() + vm.size() + vr.size());
        out.insert(out.end(), vh.begin(), vh.end());
        out.insert(out.end(), vm.begin(), vm.end());
        out.insert(out.end(), vr.begin(), vr.end());

        std::filesystem::remove(tmp_h, ec);
        if(has_mip0) std::filesystem::remove(tmp_m, ec);
        if(has_rest) std::filesystem::remove(tmp_r, ec);

        return !out.empty();
    }catch(...){
        std::filesystem::remove(tmp_h, ec);
        std::filesystem::remove(tmp_m, ec);
        std::filesystem::remove(tmp_r, ec);
        return false;
    }
}

static bool build_mdl_buffer_for_name(const std::string& mdl_name, std::vector<unsigned char>& out){
    auto p_headers = find_bnk_by_filename("globals_model_headers.bnk");
    auto p_rest    = find_bnk_by_filename("globals_models.bnk");
    if(!p_headers || !p_rest) return false;

    BNKReader r_headers(*p_headers);
    BNKReader r_rest(*p_rest);

    std::unordered_map<std::string,int> mapH, mapR;
    for(size_t i=0;i<r_headers.list_files().size();++i){ auto& e=r_headers.list_files()[i]; std::string k=e.name; std::transform(k.begin(),k.end(),k.begin(),::tolower); mapH.emplace(k,(int)i); }
    for(size_t i=0;i<r_rest.list_files().size();++i){ auto& e=r_rest.list_files()[i]; std::string k=e.name; std::transform(k.begin(),k.end(),k.begin(),::tolower); mapR.emplace(k,(int)i); }

    std::string key=mdl_name; std::transform(key.begin(),key.end(),key.begin(),::tolower);
    if(!mapH.count(key) || !mapR.count(key)) return false;

    auto tmpdir = std::filesystem::temp_directory_path() / "f2_mdl_hex";
    std::error_code ec; std::filesystem::create_directories(tmpdir, ec);
    auto tmp_h = tmpdir / "h.bin";
    auto tmp_r = tmpdir / "r.bin";
    try{
        extract_one(*p_headers, mapH.at(key), tmp_h.string());
        extract_one(*p_rest, mapR.at(key), tmp_r.string());
        auto vh = read_all_bytes(tmp_h);
        auto vr = read_all_bytes(tmp_r);
        out.clear();
        out.reserve(vh.size()+vr.size());
        out.insert(out.end(), vh.begin(), vh.end());
        out.insert(out.end(), vr.begin(), vr.end());
        std::filesystem::remove(tmp_h, ec);
        std::filesystem::remove(tmp_r, ec);
    }catch(...){
        return false;
    }
    return true;
}

static void open_hex_for_selected(){
    int idx = S.selected_file_index;
    if(idx < 0 || idx >= (int)S.files.size()){ show_error_box("No file selected."); return; }

    auto name = S.files[(size_t)idx].name;
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    bool want_tex = is_tex_file(lower);
    bool want_mdl = is_mdl_file(lower);
    if(!want_tex && !want_mdl){ show_error_box("Hex viewer supports only .tex or .mdl"); return; }

    progress_open(0, "Loading hex...");
    S.hex_loading.store(true);

    std::thread([name, want_tex, want_mdl](){
        std::vector<unsigned char> buf;
        bool ok = false;
        std::string error_detail;

        try {
            if(want_tex) {
                auto p_headers = find_bnk_by_filename("globals_texture_headers.bnk");
                auto p_rest    = find_bnk_by_filename("globals_textures.bnk");

                if(!p_headers) {
                    error_detail = "globals_texture_headers.bnk not found";
                } else if(!p_rest) {
                    error_detail = "globals_textures.bnk not found";
                } else {
                    ok = build_tex_buffer_for_name(name, buf);
                    if(!ok) {
                        error_detail = "Texture '" + name + "' not found in BNK files or extraction failed";
                    }
                }
            } else if(want_mdl) {
                auto p_headers = find_bnk_by_filename("globals_model_headers.bnk");
                auto p_rest    = find_bnk_by_filename("globals_models.bnk");

                if(!p_headers) {
                    error_detail = "globals_model_headers.bnk not found";
                } else if(!p_rest) {
                    error_detail = "globals_models.bnk not found";
                } else {
                    ok = build_mdl_buffer_for_name(name, buf);
                    if(!ok) {
                        error_detail = "Model '" + name + "' not found in BNK files or extraction failed";
                    }
                }
            }
        } catch(const std::exception& e) {
            error_detail = std::string("Exception: ") + e.what();
            ok = false;
        } catch(...) {
            error_detail = "Unknown exception occurred";
            ok = false;
        }

        S.hex_data.clear();
        if(ok) S.hex_data.swap(buf);

        S.hex_title = std::string("Hex Editor - ") + name;
        S.hex_open  = ok;

        memset(&S.hex_state, 0, sizeof(S.hex_state));
        if(ok){
            S.hex_state.Bytes        = (void*)S.hex_data.data();
            S.hex_state.MaxBytes     = (int)S.hex_data.size();
            S.hex_state.ReadOnly     = true;
            S.hex_state.ShowAscii    = true;
            S.hex_state.ShowAddress  = true;
            S.hex_state.BytesPerLine = 16;
        }

        S.hex_loading.store(false);
        progress_done();
        if(!ok) {
            std::string msg = "Failed to load bytes for hex view.";
            if(!error_detail.empty()) {
                msg += "\n\nDetails:\n" + error_detail;
            }
            show_error_box(msg);
        }
    }).detach();
}

static void draw_hex_window() {
    if(!S.hex_open) return;
    if(S.hex_loading.load()) return;
    if(S.hex_data.empty()){ S.hex_open = false; return; }

    ImGui::SetNextWindowSize(ImVec2(900, 520), ImGuiCond_FirstUseEver);
    if(ImGui::Begin(S.hex_title.c_str(), &S.hex_open))
    {
        static ImGuiHexEditorState hex{};
        unsigned char* bytes_ptr = S.hex_data.data();
        int max_bytes = (int)std::min<size_t>(S.hex_data.size(), (size_t)INT_MAX);
        if(hex.Bytes != bytes_ptr || hex.MaxBytes != max_bytes){
            hex = ImGuiHexEditorState{};
            hex.Bytes        = (void*)bytes_ptr;
            hex.MaxBytes     = max_bytes > 0 ? max_bytes : 1;
            hex.ReadOnly     = true;
            hex.ShowAscii    = true;
            hex.ShowAddress  = true;
            hex.BytesPerLine = 16;
        }
        if(hex.BytesPerLine <= 0) hex.BytesPerLine = 16;

        ImGui::BeginChild("hex_and_info", ImVec2(0,0), false);
        ImGui::BeginGroup();

        float left_w = ImGui::GetContentRegionAvail().x * 0.58f;
        if(left_w < 160.0f) left_w = ImGui::GetContentRegionAvail().x;
        ImGui::BeginChild("hex_left", ImVec2(left_w, 0), true);
        ImGui::BeginHexEditor("hex_view", &hex, ImVec2(0,0));
        ImGui::EndHexEditor();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("hex_right", ImVec2(0,0), true);

        static int cached_sel_index = -1;

        bool has_sel = (S.selected_file_index>=0 && S.selected_file_index<(int)S.files.size());
        if(has_sel){
            if(cached_sel_index != S.selected_file_index){
                if(S.preview_srv){ S.preview_srv->Release(); S.preview_srv = nullptr; }
                S.preview_mip_index = -1;
                S.show_preview_popup = false;
                S.tex_info_ok = false;
                cached_sel_index = S.selected_file_index;
            }

            std::string sel = S.files[(size_t)S.selected_file_index].name;

            if(is_tex_file(sel)){
                // Always attempt to parse
                if(!S.tex_info_ok){
                    S.tex_info_ok = parse_tex_info(S.hex_data, S.tex_info);
                }

                // Show header info even if parsing partially failed
                ImGui::Text("Header");
                ImGui::Separator();

                if(S.hex_data.size() >= 32){
                    uint32_t sign, rawsize, unk0, unk1, width, height, pixfmt, mipmap;
                    size_t o=0;
                    bool header_ok = true;
                    header_ok &= rd32be(S.hex_data, o, sign); o+=4;
                    header_ok &= rd32be(S.hex_data, o, rawsize); o+=4;
                    header_ok &= rd32be(S.hex_data, o, unk0); o+=4;
                    header_ok &= rd32be(S.hex_data, o, unk1); o+=4;
                    header_ok &= rd32be(S.hex_data, o, width); o+=4;
                    header_ok &= rd32be(S.hex_data, o, height); o+=4;
                    header_ok &= rd32be(S.hex_data, o, pixfmt); o+=4;
                    header_ok &= rd32be(S.hex_data, o, mipmap); o+=4;

                    if(header_ok){
                        ImGui::Text("Sign: 0x%08X", sign);
                        ImGui::Text("RawDataSize: %u", rawsize);
                        ImGui::Text("Unknown_0: %u", unk0);
                        ImGui::Text("Unknown_1: %u", unk1);
                        ImGui::Text("Width: %u", width);
                        ImGui::Text("Height: %u", height);
                        ImGui::Text("PixelFormat: 0x%08X", pixfmt);
                        ImGui::Text("MipMap: %u", mipmap);
                    }else{
                        ImGui::TextColored(ImVec4(1,0.5f,0.5f,1), "Failed to read header");
                    }
                }else{
                    ImGui::TextColored(ImVec4(1,0.5f,0.5f,1), "File too small (< 32 bytes)");
                }

                if(S.tex_info_ok && !S.tex_info.Mips.empty()){
                    ImGui::Dummy(ImVec2(0,6));
                    ImGui::Text("MipMap Definitions");
                    ImGui::Separator();

                    for(int i=0;i<(int)S.tex_info.Mips.size();++i){
                        const auto& m = S.tex_info.Mips[i];
                        char lbl[64]; snprintf(lbl,sizeof(lbl),"Mip %d", i);
                        if(ImGui::TreeNode(lbl)){
                            ImGui::Text("DefOffset: 0x%zX", m.DefOffset);
                            ImGui::Text("CompFlag: %u", m.CompFlag);
                            ImGui::Text("DataOffset: 0x%08X", m.DataOffset);
                            ImGui::Text("DataSize: %u", m.DataSize);
                            ImGui::Text("Unknown_3: %u", m.Unknown_3);
                            ImGui::Text("Unknown_4: %u", m.Unknown_4);
                            ImGui::Text("Unknown_5: %u", m.Unknown_5);
                            ImGui::Text("Unknown_6: %u", m.Unknown_6);
                            ImGui::Text("Unknown_7: %u", m.Unknown_7);
                            ImGui::Text("Unknown_8: %u", m.Unknown_8);
                            ImGui::Text("Unknown_9: %u", m.Unknown_9);
                            ImGui::Text("Unknown_10: %u", m.Unknown_10);
                            ImGui::Text("Unknown_11: %u", m.Unknown_11);
                            if(m.HasWH){
                                ImGui::Text("MipWidth: %u", (unsigned)m.MipWidth);
                                ImGui::Text("MipHeight: %u", (unsigned)m.MipHeight);
                            }else{
                                uint32_t w = std::max(1u, S.tex_info.TextureWidth  >> i);
                                uint32_t h = std::max(1u, S.tex_info.TextureHeight >> i);
                                ImGui::Text("Derived Size: %ux%u", w, h);
                            }
                            ImGui::Text("MipMapData@ 0x%zX, Size %zu", m.MipDataOffset, m.MipDataSizeParsed);

                            if(m.CompFlag == 7){
                                if(ImGui::Button("Preview")){
                                    S.preview_mip_index = i;
                                    S.show_preview_popup = true;
                                }
                            }

                            ImGui::TreePop();
                        }
                    }
                }else if(S.hex_data.size() >= 32){
                    ImGui::Dummy(ImVec2(0,6));
                    ImGui::TextColored(ImVec4(1,0.7f,0.3f,1), "Mipmap parsing failed");
                    ImGui::TextWrapped("Could not parse mipmap definitions. File may be corrupted or incomplete.");
                }
            }else if(is_mdl_file(sel)){
                ImGui::TextUnformatted(".mdl selected");
            }else{
                ImGui::TextUnformatted("No parsed info");
            }
        }else{
            ImGui::TextUnformatted("No file selected");
        }
        ImGui::EndChild();

        ImGui::EndGroup();
        ImGui::EndChild();
    }
    ImGui::End();

    // Preview popup window - render outside the main hex window
    if(S.show_preview_popup){
        ImGui::OpenPopup("Mip Preview");
        S.show_preview_popup = false; // Only open once
    }

    if(ImGui::BeginPopupModal("Mip Preview", nullptr, ImGuiWindowFlags_None)){
        if(S.preview_mip_index >= 0 && S.preview_mip_index < (int)S.tex_info.Mips.size()){
            const auto& m = S.tex_info.Mips[S.preview_mip_index];

            if(!S.preview_srv){
                uint32_t base_w = S.tex_info.TextureWidth;
                uint32_t base_h = S.tex_info.TextureHeight;
                uint32_t w = m.HasWH ? (uint32_t)std::max(1,(int)m.MipWidth)  : std::max(1u, base_w >> S.preview_mip_index);
                uint32_t h = m.HasWH ? (uint32_t)std::max(1,(int)m.MipHeight) : std::max(1u, base_h >> S.preview_mip_index);

                if(m.MipDataOffset < S.hex_data.size() && m.MipDataOffset + m.MipDataSizeParsed <= S.hex_data.size()){
                    const uint8_t* src = S.hex_data.data() + m.MipDataOffset;
                    size_t src_sz = m.MipDataSizeParsed;

                    size_t blocks_x = (w + 3) / 4;
                    size_t blocks_y = (h + 3) / 4;
                    size_t bc1_sz = blocks_x * blocks_y * 8;
                    size_t bc5_sz = blocks_x * blocks_y * 16;

                    DXGI_FORMAT fmt = DXGI_FORMAT_UNKNOWN;
                    std::vector<uint8_t> payload;

                    if(src_sz == bc1_sz){
                        payload.resize(bc1_sz);
                        for(size_t i=0;i<bc1_sz;i+=8){
                            uint16_t c0 = (uint16_t)((src[i+0]<<8) | src[i+1]);
                            uint16_t c1 = (uint16_t)((src[i+2]<<8) | src[i+3]);
                            uint32_t idx = (uint32_t)((src[i+4]<<24) | (src[i+5]<<16) | (src[i+6]<<8) | src[i+7]);
                            payload[i+0] = (uint8_t)(c0 & 0xFF);
                            payload[i+1] = (uint8_t)(c0 >> 8);
                            payload[i+2] = (uint8_t)(c1 & 0xFF);
                            payload[i+3] = (uint8_t)(c1 >> 8);
                            payload[i+4] = (uint8_t)(idx & 0xFF);
                            payload[i+5] = (uint8_t)((idx >> 8) & 0xFF);
                            payload[i+6] = (uint8_t)((idx >> 16) & 0xFF);
                            payload[i+7] = (uint8_t)((idx >> 24) & 0xFF);
                        }
                        fmt = DXGI_FORMAT_BC1_UNORM;
                    }else if(src_sz == bc5_sz){
                        payload.assign(src, src + src_sz);
                        fmt = DXGI_FORMAT_BC5_UNORM;
                    }

                    if(fmt != DXGI_FORMAT_UNKNOWN){
                        D3D11_TEXTURE2D_DESC td{};
                        td.Width = w;
                        td.Height = h;
                        td.MipLevels = 1;
                        td.ArraySize = 1;
                        td.Format = fmt;
                        td.SampleDesc.Count = 1;
                        td.Usage = D3D11_USAGE_DEFAULT;
                        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

                        D3D11_SUBRESOURCE_DATA srd{};
                        srd.pSysMem = payload.data();
                        srd.SysMemPitch = (UINT)(blocks_x * (fmt==DXGI_FORMAT_BC1_UNORM?8:16));
                        srd.SysMemSlicePitch = (UINT)payload.size();

                        ID3D11Texture2D* tex = nullptr;
                        HRESULT hr = g_pd3dDevice->CreateTexture2D(&td, &srd, &tex);
                        if(SUCCEEDED(hr) && tex){
                            D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
                            srvd.Format = td.Format;
                            srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
                            srvd.Texture2D.MipLevels = 1;

                            hr = g_pd3dDevice->CreateShaderResourceView(tex, &srvd, &S.preview_srv);
                            tex->Release();
                        }
                    }
                }
            }

            if(S.preview_srv){
                uint32_t base_w = S.tex_info.TextureWidth;
                uint32_t base_h = S.tex_info.TextureHeight;
                uint32_t w = m.HasWH ? (uint32_t)std::max(1,(int)m.MipWidth)  : std::max(1u, base_w >> S.preview_mip_index);
                uint32_t h = m.HasWH ? (uint32_t)std::max(1,(int)m.MipHeight) : std::max(1u, base_h >> S.preview_mip_index);

                ImGui::Text("Mip %d (%ux%u)", S.preview_mip_index, (unsigned)w, (unsigned)h);

                ImVec2 avail = ImGui::GetContentRegionAvail();
                avail.y -= ImGui::GetFrameHeightWithSpacing();

                float aspect = (float)w / (float)h;
                float display_w = avail.x;
                float display_h = display_w / aspect;

                if(display_h > avail.y){
                    display_h = avail.y;
                    display_w = display_h * aspect;
                }

                ImGui::BeginChild("preview_image", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false);
                ImGui::Image((ImTextureID)S.preview_srv, ImVec2(display_w, display_h));
                ImGui::EndChild();
            }else{
                ImGui::Text("Preview unavailable for Mip %d", S.preview_mip_index);
                ImGui::Dummy(ImVec2(200, 50));
            }
        }

        if(ImGui::Button("Close", ImVec2(120, 0))){
            if(S.preview_srv){ S.preview_srv->Release(); S.preview_srv = nullptr; }
            S.preview_mip_index = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

static bool name_matches_filter(const std::string& name, const std::string& filter) {
    if(filter.empty()) return true;
    std::string n=name, f=filter;
    std::transform(n.begin(),n.end(),n.begin(),::tolower);
    std::transform(f.begin(),f.end(),f.begin(),::tolower);
    return n.find(f)!=std::string::npos;
}

static int count_visible_files(){
    int c=0;
    for(auto& f: S.files) if(name_matches_filter(f.name, S.file_filter)) ++c;
    return c;
}

static bool any_wav_in_bnk(){
    for(auto& f: S.files) if(is_audio_file(f.name)) return true;
    return false;
}

static bool any_tex_in_bnk(){
    for(auto& f: S.files) if(is_tex_file(f.name)) return true;
    return false;
}

static bool is_texture_bnk_selected(){
    if(S.selected_bnk.empty()) return false;
    std::string b = std::filesystem::path(S.selected_bnk).filename().string();
    std::transform(b.begin(),b.end(),b.begin(),::tolower);
    return b=="globals_texture_headers.bnk" || b=="1024mip0_textures.bnk" || b=="globals_textures.bnk";
}

static void draw_left_panel(){
    ImGui::BeginChild("left_panel", ImVec2(360,0), true);
    ImGui::SetNextItemWidth(-1);
    if(!S.bnk_paths.empty()){
        ImGui::InputTextWithHint("##bnk_filter", "Filter", &S.bnk_filter);
    }
    ImGui::BeginChild("bnk_list", ImVec2(0,0), false);
    auto paths = filtered_bnk_paths();
    for(auto& p: paths){
        std::string label = std::filesystem::path(p).filename().string();
        if(ImGui::Selectable(label.c_str(), p==S.selected_bnk, ImGuiSelectableFlags_SpanAllColumns)) pick_bnk(p);
        if(!S.hide_tooltips && ImGui::IsItemHovered()){
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(p.c_str());
            ImGui::EndTooltip();
        }
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

static void draw_file_table(){
    std::vector<int> vis;
    vis.reserve(S.files.size());
    for(size_t i=0;i<S.files.size();++i)
        if(name_matches_filter(S.files[i].name, S.file_filter)) vis.push_back((int)i);

    ImGuiTable* tbl_ptr = nullptr;
    if(ImGui::BeginTable("files_table", 2, ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV|ImGuiTableFlags_BordersOuter|ImGuiTableFlags_SizingStretchProp)){
        tbl_ptr = ImGui::GetCurrentTable();
        ImGui::TableSetupColumn("File");
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 140.0f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin((int)vis.size());
        while(clipper.Step()){
            for(int r=clipper.DisplayStart; r<clipper.DisplayEnd; ++r){
                int i = vis[r];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                bool selected = (i==S.selected_file_index);
                std::string base = std::filesystem::path(S.files[i].name).filename().string();
                if(ImGui::Selectable(base.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
                    S.selected_file_index=i;
                if(!S.hide_tooltips && ImGui::IsItemHovered()){
                    ImGui::BeginTooltip();
                    ImGui::TextUnformatted(S.files[i].name.c_str());
                    ImGui::EndTooltip();
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%u", S.files[i].size);
            }
        }
        clipper.End();
        ImGui::EndTable();
    }
    if(tbl_ptr){
        ImRect r = tbl_ptr->OuterRect;
        ImU32 col = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Border));
        ImGui::GetWindowDrawList()->AddRect(r.Min, r.Max, col, 8.0f, 0, 1.0f);
    }
}

static void draw_folder_dialog(){
    ImVec2 vp = ImGui::GetMainViewport()->WorkSize;
    ImVec2 minSize(680, 440);
    ImVec2 maxSize(vp.x * 0.9f, vp.y * 0.9f);
    if(ImGuiFileDialog::Instance()->Display("PickDir", ImGuiWindowFlags_NoCollapse, minSize, maxSize))
    {
        if(ImGuiFileDialog::Instance()->IsOk())
        {
            std::string sel = ImGuiFileDialog::Instance()->GetCurrentPath();
            open_folder_logic(sel);
        }
        ImGuiFileDialog::Instance()->Close();
    }
}

static void draw_right_panel(){
    ImGui::BeginChild("right_panel", ImVec2(0,0), false);

    ImGui::BeginChild("extract_box", ImVec2(0,80), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginGroup();
    ImGui::PushItemWidth(-1);

    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8,0));

    ImGui::BeginGroup();
    if(ImGui::Button("Dump All Files")){ ImGui::OpenPopup("progress_win"); on_dump_all_raw(); }
    if(!S.hide_tooltips && ImGui::IsItemHovered()){ ImGui::BeginTooltip(); ImGui::TextUnformatted("DUMPS ALL FILES IN THE CURRENT BANK"); ImGui::EndTooltip(); }
    ImGui::SameLine();
    if(any_wav_in_bnk()) if(ImGui::Button("Export WAV's")){ ImGui::OpenPopup("progress_win"); on_export_wavs(); }
    if(any_wav_in_bnk() && !S.hide_tooltips && ImGui::IsItemHovered()){ ImGui::BeginTooltip(); ImGui::TextUnformatted("Convert and export only the .wav files"); ImGui::EndTooltip(); }
    if(is_texture_bnk_selected() && any_tex_in_bnk()){
        ImGui::SameLine();
        if(ImGui::Button("Rebuild and Extract All")){ ImGui::OpenPopup("progress_win"); on_rebuild_and_extract(); }
        if(!S.hide_tooltips && ImGui::IsItemHovered()){ ImGui::BeginTooltip(); ImGui::TextUnformatted("Rebuilds the .tex file bitstreams"); ImGui::EndTooltip(); }
    }

    if(S.selected_file_index>=0){
        ImGui::SameLine();
        if(ImGui::Button("Extract File")){ ImGui::OpenPopup("progress_win"); on_extract_selected_raw(); }
        ImGui::SameLine();
        bool can_wav=false;
        if(S.selected_file_index>=0 && S.selected_file_index<(int)S.files.size()){
            std::string n=S.files[(size_t)S.selected_file_index].name;
            std::string l=n; std::transform(l.begin(),l.end(),l.begin(),::tolower);
            can_wav = l.size()>=4 && l.rfind(".wav")==l.size()-4;
        }
        if(can_wav){
            if(ImGui::Button("Extract WAV")){ ImGui::OpenPopup("progress_win"); on_extract_selected_wav(); }
            ImGui::SameLine();
        }
        bool can_tex=false, can_mdl=false;
        if(S.selected_file_index>=0 && S.selected_file_index<(int)S.files.size()){
            std::string n=S.files[(size_t)S.selected_file_index].name;
            std::string l=n; std::transform(l.begin(),l.end(),l.begin(),::tolower);
            can_tex = l.size()>=4 && l.rfind(".tex")==l.size()-4;
            can_mdl = l.size()>=4 && l.rfind(".mdl")==l.size()-4;
        }
        if(can_tex && is_texture_bnk_selected()){
            if(ImGui::Button("Rebuild and Extract")) {
                auto name = S.files[(size_t)S.selected_file_index].name;
                ImGui::OpenPopup("progress_win");
                on_rebuild_and_extract_one(name);
            }
            if(!S.hide_tooltips && ImGui::IsItemHovered()){ ImGui::BeginTooltip(); ImGui::TextUnformatted("Rebuilds the .tex file bitstreams"); ImGui::EndTooltip(); }
            ImGui::SameLine();
        }
        if(can_tex || can_mdl){
            if(ImGui::Button("Hex View")){ ImGui::OpenPopup("progress_win"); open_hex_for_selected(); }
        } else {
            ImGui::InvisibleButton("hex_hidden", ImVec2(1,1));
        }
    }
    ImGui::EndGroup();

    ImGui::PopStyleVar();
    ImGui::EndGroup();

    static bool hide_tt=false;
    if(ImGui::Checkbox("Hide Paths Tooltip", &hide_tt)){ S.hide_tooltips=hide_tt; }

    int visible = count_visible_files();
    ImGui::Text("Files found: %d/%d", visible, (int)S.files.size());

    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::EndChild();

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##file_filter", "Filter", &S.file_filter);

    ImGui::BeginChild("right_table_container", ImVec2(0,0), false);
    draw_file_table();
    ImGui::EndChild();

    ImGui::EndChild();
}

static void draw_main(HWND hwnd){
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float inset = 8.0f;
    ImGui::SetNextWindowPos(vp->WorkPos + ImVec2(inset,inset));
    ImGui::SetNextWindowSize(vp->WorkSize - ImVec2(inset*2,inset*2));
    ImGui::Begin("Fable 2 Asset Browser", nullptr, ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus);
    if(S.root_dir.empty()){
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 sz(320,50);
        ImVec2 pos((avail.x - sz.x)*0.5f, (avail.y - sz.y)*0.5f);
        ImGui::SetCursorPos(pos);
        if(ImGui::Button("Select Fable 2 Directory", sz)){
            IGFD::FileDialogConfig cfg;
            std::string base = (!S.last_dir.empty() && std::filesystem::exists(S.last_dir) && std::filesystem::is_directory(S.last_dir)) ? S.last_dir : ".";
            cfg.path = base.c_str();
            ImGuiFileDialog::Instance()->OpenDialog("PickDir", "Select Fable 2 Directory", nullptr, cfg);
        }
        draw_folder_dialog();
    }else{
        ImGui::BeginChild("browser_group", ImVec2(0,0), false);
        ImGui::BeginGroup();
        ImGui::BeginChild("left_panel_wrap", ImVec2(360,0), false);
        draw_left_panel();
        ImGui::EndChild();
        ImGui::SameLine();
        draw_right_panel();
        ImGui::EndGroup();
        ImGui::EndChild();
    }
    ImGui::End();
}

static void build_theme(){
    auto& s = ImGui::GetStyle();
    s.WindowRounding   = 12.0f;
    s.FrameRounding    = 8.0f;
    s.ChildRounding    = 10.0f;
    s.PopupRounding    = 10.0f;
    s.GrabRounding     = 8.0f;
    s.TabRounding      = 8.0f;
    s.ScrollbarRounding= 8.0f;
    s.WindowBorderSize = 0.0f;
}

int main(){
    HINSTANCE hInstance = GetModuleHandle(nullptr);
    WNDCLASSEXA wc{};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon   = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_ICON1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_ICON1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYICON), 0);
    wc.lpszClassName = "BNKWndClass";
    RegisterClassExA(&wc);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "Fable 2 Asset Browser", WS_OVERLAPPEDWINDOW, 100, 100, 1100, 680, NULL, NULL, wc.hInstance, NULL);
    HICON big = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_ICON1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    HICON sml = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_ICON1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYICON), 0);
    SendMessageA(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)big);
    SendMessageA(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)sml);
    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); UnregisterClassA(wc.lpszClassName, wc.hInstance); return 1; }
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    build_theme();
    S.last_dir = load_last_dir();

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
            if (msg.message == WM_QUIT) done = true;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (done) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        draw_main(hwnd);
        draw_folder_dialog();
        if (S.show_progress.load()) ImGui::OpenPopup("progress_win");
        if (S.show_error) { ImGui::OpenPopup("error_modal"); S.show_error = false; }
        if (S.show_completion) { ImGui::OpenPopup("completion_modal"); S.show_completion = false; }

        ImGuiViewport* vp = ImGui::GetMainViewport();
        float w = std::clamp(vp->WorkSize.x*0.6f, 520.0f, 900.0f);
        const ImGuiStyle& st = ImGui::GetStyle();
        float line = ImGui::GetTextLineHeightWithSpacing();
        float h = st.WindowPadding.y*2.0f + line + st.ItemSpacing.y + (line*2.0f + 6.0f) + st.ItemSpacing.y + ImGui::GetFrameHeight() + st.ItemSpacing.y + ImGui::GetFrameHeight() + 12.0f;
        ImGui::SetNextWindowSize(ImVec2(w,h), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(vp->WorkPos + ImVec2((vp->WorkSize.x - w)*0.5f, (vp->WorkSize.y - h)*0.5f), ImGuiCond_Appearing);
        if(ImGui::BeginPopupModal("progress_win", nullptr, ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoTitleBar)){
            int total, current; std::string label;
            { std::lock_guard<std::mutex> lk(S.progress_mutex); total=S.progress_total; current=S.progress_current; label=S.progress_label; }
            ImGui::Text("%d/%d", current, std::max(1,total));
            float wrap_w = ImGui::GetContentRegionAvail().x;
            std::string two = label;
            if(ImGui::CalcTextSize(two.c_str()).x > wrap_w){
                size_t mid = two.size()/2;
                auto fits=[&](size_t pos){ std::string a=two.substr(0,pos), b=two.substr(pos+1); return ImGui::CalcTextSize(a.c_str()).x<=wrap_w && ImGui::CalcTextSize(b.c_str()).x<=wrap_w; };
                size_t cand = std::string::npos;
                size_t l1 = two.rfind('\\', mid), l2 = two.rfind('/', mid);
                if(l1!=std::string::npos || l2!=std::string::npos) cand = std::max(l1==std::string::npos?0:l1, l2==std::string::npos?0:l2);
                if(cand!=std::string::npos && fits(cand)) two.insert(cand+1,"\n");
                else{
                    size_t r1 = two.find('\\', mid), r2 = two.find('/', mid);
                    size_t r = std::min(r1==std::string::npos?two.size():r1, r2==std::string::npos?two.size():r2);
                    if(r!=two.size() && fits(r)) two.insert(r+1,"\n"); else two.insert(mid,"\n");
                }
            }
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+wrap_w);
            ImGui::BeginChild("progress_label", ImVec2(0, ImGui::GetTextLineHeightWithSpacing()*2.0f + 6.0f), false, ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::TextUnformatted(two.c_str());
            ImGui::EndChild();
            ImGui::PopTextWrapPos();
            float frac = total>0? (float)current/(float)total : 1.0f;
            ImGui::ProgressBar(frac, ImVec2(-1,0));
            ImGui::Dummy(ImVec2(0,6));
            if(ImGui::Button("Cancel", ImVec2(-1,0))){ S.cancel_requested=true; progress_done(); show_completion_box("Extraction cancelled."); }
            if(!S.show_progress.load()) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGuiViewport* vp2 = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp2->WorkPos + vp2->WorkSize*0.5f, ImGuiCond_Always, ImVec2(0.5f,0.5f));
        if(ImGui::BeginPopupModal("error_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoTitleBar)){
            ImGui::TextColored(ImVec4(1,0.47f,0.47f,1),"Error");
            ImGui::Separator();
            ImGui::PushTextWrapPos(ImGui::GetFontSize()*40.0f);
            ImGui::TextUnformatted(S.error_text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0,10));
            if(ImGui::Button("Close", ImVec2(-1,0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGuiViewport* vp3 = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp3->WorkPos + vp3->WorkSize*0.5f, ImGuiCond_Always, ImVec2(0.5f,0.5f));
        if(ImGui::BeginPopupModal("completion_modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoTitleBar)){
            ImGui::TextColored(ImVec4(0.47f,1,0.47f,1),"Operation Status");
            ImGui::Separator();
            ImGui::PushTextWrapPos(ImGui::GetFontSize()*40.0f);
            ImGui::TextUnformatted(S.completion_text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Dummy(ImVec2(0,10));
            if(ImGui::Button("OK", ImVec2(-1,0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        draw_hex_window();

        ImGui::Render();
        const float clear_color[4] = {0.10f,0.10f,0.10f,1.0f};
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return 0;
}

