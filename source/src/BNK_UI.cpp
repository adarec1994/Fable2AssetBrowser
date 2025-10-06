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
} S;

static bool is_audio_file(const std::string& n){ std::string s=n; std::transform(s.begin(),s.end(),s.begin(),::tolower); return s.size()>=4 && s.rfind(".wav")==s.size()-4; }
static bool is_tex_file(const std::string& n){ std::string s=n; std::transform(s.begin(),s.end(),s.begin(),::tolower); return s.size()>=4 && s.rfind(".tex")==s.size()-4; }

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

static void draw_error_modal(){
    if(S.show_error){ ImGui::OpenPopup("error_modal"); S.show_error=false; }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos + vp->WorkSize*0.5f, ImGuiCond_Always, ImVec2(0.5f,0.5f));
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
}

static void draw_completion_modal(){
    if(S.show_completion){ ImGui::OpenPopup("completion_modal"); S.show_completion=false; }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos + vp->WorkSize*0.5f, ImGuiCond_Always, ImVec2(0.5f,0.5f));
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
}

static void draw_progress_modal(){
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float w = std::clamp(vp->WorkSize.x*0.6f, 520.0f, 900.0f);
    const ImGuiStyle& st = ImGui::GetStyle();
    float line = ImGui::GetTextLineHeightWithSpacing();
    float h = st.WindowPadding.y*2.0f + line + st.ItemSpacing.y + (line*2.0f + 6.0f) + st.ItemSpacing.y + ImGui::GetFrameHeight() + st.ItemSpacing.y + ImGui::GetFrameHeight() + 12.0f;
    if(S.show_progress.load()) ImGui::OpenPopup("progress_win");
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

static std::optional<std::string> find_bnk_by_filename(const std::string& fname_lower){
    for(auto& p: S.bnk_paths){
        std::string b = std::filesystem::path(p).filename().string();
        std::transform(b.begin(),b.end(),b.begin(),::tolower);
        if(b==fname_lower) return p;
    }
    return std::nullopt;
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
    auto p_rest    = find_bnk_by_filename("global_textures.bnk");
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
        bool can_tex=false;
        if(S.selected_file_index>=0 && S.selected_file_index<(int)S.files.size()){
            std::string n=S.files[(size_t)S.selected_file_index].name;
            std::string l=n; std::transform(l.begin(),l.end(),l.begin(),::tolower);
            can_tex = l.size()>=4 && l.rfind(".tex")==l.size()-4;
        }
        if(can_tex && is_texture_bnk_selected()){
            if(ImGui::Button("Rebuild and Extract")) {
                auto name = S.files[(size_t)S.selected_file_index].name;
                ImGui::OpenPopup("progress_win");
                on_rebuild_and_extract_one(name);
            }
            if(!S.hide_tooltips && ImGui::IsItemHovered()){ ImGui::BeginTooltip(); ImGui::TextUnformatted("Rebuilds the .tex file bitstreams"); ImGui::EndTooltip(); }
        } else {
            ImGui::InvisibleButton("rebuild_single_hidden", ImVec2(1,1));
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
        draw_progress_modal();
        draw_error_modal();
        draw_completion_modal();

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
