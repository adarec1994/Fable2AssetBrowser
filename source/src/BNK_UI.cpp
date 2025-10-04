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
} S;

static bool is_audio_file(const std::string& n){ std::string s=n; std::transform(s.begin(),s.end(),s.begin(),::tolower); return s.size()>=4 && s.rfind(".wav")==s.size()-4; }

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

static std::string PickFolderWin32(HWND owner){
    std::string out;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileDialog* pfd = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pfd)))) {
        DWORD opts=0; pfd->GetOptions(&opts); pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (SUCCEEDED(pfd->Show(owner))) {
            IShellItem* psi = nullptr;
            if (SUCCEEDED(pfd->GetResult(&psi))) {
                PWSTR wpath=nullptr;
                if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &wpath))) {
                    int len = WideCharToMultiByte(CP_UTF8,0,wpath,-1,nullptr,0,nullptr,nullptr);
                    std::string s(len-1,0);
                    WideCharToMultiByte(CP_UTF8,0,wpath,-1,s.data(),len,nullptr,nullptr);
                    out = s;
                    CoTaskMemFree(wpath);
                }
                psi->Release();
            }
        }
        pfd->Release();
    }
    CoUninitialize();
    return out;
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
    ImVec2 sz(640,200);
    ImGui::SetNextWindowSize(sz);
    ImGui::SetNextWindowPos(vp->WorkPos + (vp->WorkSize - sz)*0.5f);
    if(ImGui::BeginPopupModal("error_modal", nullptr, ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoTitleBar)){
        ImGui::TextColored(ImVec4(1,0.47f,0.47f,1),"Error");
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth()-40);
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
    ImVec2 sz(640,220);
    ImGui::SetNextWindowSize(sz);
    ImGui::SetNextWindowPos(vp->WorkPos + (vp->WorkSize - sz)*0.5f);
    if(ImGui::BeginPopupModal("completion_modal", nullptr, ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoTitleBar)){
        ImGui::TextColored(ImVec4(0.47f,1,0.47f,1),"Operation Status");
        ImGui::Separator();
        ImGui::PushTextWrapPos(ImGui::GetWindowWidth()-40);
        ImGui::TextUnformatted(S.completion_text.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0,10));
        if(ImGui::Button("OK", ImVec2(-1,0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

static void draw_progress_modal(){
    if(S.show_progress.load()) ImGui::OpenPopup("progress_win");
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 sz(520,140);
    ImGui::SetNextWindowSize(sz);
    ImGui::SetNextWindowPos(vp->WorkPos + (vp->WorkSize - sz)*0.5f);
    if(ImGui::BeginPopupModal("progress_win", nullptr, ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoTitleBar)){
        int total, current; std::string label;
        { std::lock_guard<std::mutex> lk(S.progress_mutex); total=S.progress_total; current=S.progress_current; label=S.progress_label; }
        ImGui::Text("%s", (std::string(std::to_string(current)+"/"+std::to_string(std::max(1,total))+"   "+label)).c_str());
        float frac = total>0? (float)current/(float)total : 1.0f;
        ImGui::ProgressBar(frac, ImVec2(-1,0));
        ImGui::Dummy(ImVec2(0,6));
        if(ImGui::Button("Cancel", ImVec2(-1,0))){ S.cancel_requested=true; progress_done(); show_completion_box("Extraction cancelled by user."); }
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

static void draw_right_panel(){
    ImGui::BeginChild("right_panel", ImVec2(0,0), false);

    ImGui::BeginChild("extract_box", ImVec2(0,80), true, ImGuiWindowFlags_NoScrollbar);
    ImGui::BeginGroup();
    ImGui::PushItemWidth(-1);

    ImGui::BeginGroup();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8,0));

    ImGui::BeginGroup();
    if(S.selected_file_index<0){
        if(ImGui::Button("Dump All Files")) on_dump_all_raw();
        ImGui::SameLine();
        if(any_wav_in_bnk()) if(ImGui::Button("Export WAV's")) on_export_wavs();
    }else{
        if(ImGui::Button("Extract File")) on_extract_selected_raw();
        ImGui::SameLine();
        bool can_wav=false;
        if(S.selected_file_index>=0 && S.selected_file_index<(int)S.files.size()){
            std::string n=S.files[(size_t)S.selected_file_index].name;
            std::string l=n; std::transform(l.begin(),l.end(),l.begin(),::tolower);
            can_wav = l.size()>=4 && l.rfind(".wav")==l.size()-4;
        }
        if(can_wav){
            if(ImGui::Button("Extract WAV")) on_extract_selected_wav();
        }else{
            ImGui::InvisibleButton("extract_wav_hidden", ImVec2(1,1));
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
    ImGui::Begin("FABLE2", nullptr, ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus);
    if(S.root_dir.empty()){
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 sz(320,50);
        ImVec2 pos((avail.x - sz.x)*0.5f, (avail.y - sz.y)*0.5f);
        ImGui::SetCursorPos(pos);
        if(ImGui::Button("Select Fable 2 Directory", sz)){
            std::string sel = PickFolderWin32(hwnd);
            open_folder_logic(sel);
        }
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
    wc.hIconSm = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_ICON1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpszClassName = "BNKWndClass";
    RegisterClassExA(&wc);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "FABLE2", WS_OVERLAPPEDWINDOW, 100, 100, 1100, 680, NULL, NULL, wc.hInstance, NULL);
    HICON big = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_ICON1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    HICON sml = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(IDI_ICON1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
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
        if(S.show_progress.load()) ImGui::OpenPopup("progress_win");
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
