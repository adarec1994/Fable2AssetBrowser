// audio.cpp
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <optional>
#include <algorithm>
#ifdef _WIN32
#include <windows.h>
#endif

static const uint8_t RIFF_[4] = {'R','I','F','F'};
static const uint8_t WAVE_[4] = {'W','A','V','E'};

static std::string DEFAULT_TOWAV_DIR = std::filesystem::path("include/towav").string();
static std::vector<std::string> TOWAV_EXE_NAMES = {"towav.exe","towav"};

static uint32_t read_u32le(const std::vector<uint8_t>& b, size_t off){ return (uint32_t)b[off] | ((uint32_t)b[off+1]<<8) | ((uint32_t)b[off+2]<<16) | ((uint32_t)b[off+3]<<24); }
static void write_u32le(std::vector<uint8_t>& m, size_t off, uint32_t v){ if(m.size()<off+4) m.resize(off+4); m[off]=uint8_t(v&0xFF); m[off+1]=uint8_t((v>>8)&0xFF); m[off+2]=uint8_t((v>>16)&0xFF); m[off+3]=uint8_t((v>>24)&0xFF); }

static bool has_riff_wave(const std::vector<uint8_t>& b){ return b.size()>=12 && std::equal(RIFF_,RIFF_+4,b.data()) && std::equal(WAVE_,WAVE_+4,b.data()+8); }
static bool starts_with_xma_magic(const std::vector<uint8_t>& b){ return b.size()>=4 && b[0]=='x' && b[1]=='m' && b[2]=='a' && b[3]==0; }

static void fix_riff_size(std::vector<uint8_t>& buf){ if(buf.size()>=8 && std::equal(RIFF_,RIFF_+4,buf.data())) write_u32le(buf,4,(uint32_t)((buf.size()-8)&0xFFFFFFFF)); }

struct ChunkInfo{ uint32_t id; size_t pos; uint32_t size; size_t data_start; size_t data_end; size_t data_end_padded; };

static std::vector<ChunkInfo> parse_chunks(const std::vector<uint8_t>& b, size_t start){
    std::vector<ChunkInfo> chunks; size_t pos=start; size_t n=b.size();
    while(pos+8<=n){
        uint32_t ck_id = (uint32_t)b[pos] | ((uint32_t)b[pos+1]<<8) | ((uint32_t)b[pos+2]<<16) | ((uint32_t)b[pos+3]<<24);
        uint32_t ck_size = read_u32le(b,pos+4);
        size_t data_start = pos+8;
        size_t data_end = data_start + ck_size;
        size_t data_end_padded = data_end + (ck_size & 1u);
        if(data_start>n) break;
        chunks.push_back({ck_id,pos,ck_size,data_start,data_end,data_end_padded});
        if(data_end_padded<=pos) break;
        pos = data_end_padded;
    }
    return chunks;
}

static std::pair<std::vector<uint8_t>, bool> repair_wave(const std::vector<uint8_t>& buf){
    size_t i=0; size_t wave_start=std::string::npos;
    while(true){
        const uint8_t* p = (const uint8_t*)memchr(buf.data()+i,'R', buf.size()>i? buf.size()-i : 0);
        if(!p) return {buf,false};
        size_t j = (size_t)(p - buf.data());
        if(j+12<=buf.size() && std::equal(RIFF_,RIFF_+4,buf.data()+j) && std::equal(WAVE_,WAVE_+4,buf.data()+j+8)){ wave_start=j; break; }
        i = j+1;
    }
    std::vector<uint8_t> out = wave_start? std::vector<uint8_t>(buf.begin()+wave_start, buf.end()) : buf;
    if(out.size()<12 || !std::equal(RIFF_,RIFF_+4,out.data()) || !std::equal(WAVE_,WAVE_+4,out.data()+8)) return {out,false};
    uint32_t expected_riff_size = (uint32_t)std::max<int64_t>(0,(int64_t)out.size()-8);
    bool changed=false;
    if(read_u32le(out,4)!=expected_riff_size){ write_u32le(out,4,expected_riff_size); changed=true; }
    auto chunks = parse_chunks(out,12);
    bool modified=false;
    for(auto& ck: chunks){
        if(ck.data_end > out.size()){
            uint32_t new_size = (uint32_t)std::max<int64_t>(0, (int64_t)out.size() - (int64_t)(ck.pos+8));
            write_u32le(out, ck.pos+4, new_size);
            modified=true;
        }
    }
    if(modified){ expected_riff_size = (uint32_t)std::max<int64_t>(0,(int64_t)out.size()-8); write_u32le(out,4,expected_riff_size); changed=true; }
    return {out,changed};
}

static std::optional<std::string> which(const std::string& name){
#ifdef _WIN32
    const char sep=';';
    std::vector<std::string> names = {name};
    if(name.find('.')==std::string::npos) names.push_back(name + ".exe");
#else
    const char sep=':';
    std::vector<std::string> names = {name};
#endif
    const char* penv = std::getenv("PATH");
    if(!penv) return std::nullopt;
    std::string path = penv;
    size_t start=0;
    while(start<=path.size()){
        size_t end = path.find(sep,start);
        std::string dir = path.substr(start, end==std::string::npos? std::string::npos : end-start);
        for(const auto& nm: names){
            std::filesystem::path p = std::filesystem::path(dir) / nm;
            std::error_code ec;
            if(std::filesystem::exists(p,ec)) return p.string();
        }
        if(end==std::string::npos) break;
        start = end+1;
    }
    return std::nullopt;
}

#ifdef _WIN32
static std::filesystem::path exe_dir(){
    wchar_t buf[MAX_PATH]; GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
}
#endif

static std::optional<std::string> find_towav(const std::optional<std::filesystem::path>& towav_dir){
    auto try_in_dirs = [&](const std::vector<std::filesystem::path>& dirs)->std::optional<std::string>{
        for(const auto& d: dirs){
            if(d.empty()) continue;
            std::error_code ec;
            if(!std::filesystem::exists(d,ec)) continue;
            for(auto& n: TOWAV_EXE_NAMES){
                auto f = d / n;
                if(std::filesystem::exists(f,ec)) return f.string();
            }
        }
        return std::nullopt;
    };

    if(towav_dir){
        if(auto r = try_in_dirs({*towav_dir})) return r;
    }

#ifdef _WIN32
    auto ed = exe_dir();
#else
    auto ed = std::filesystem::current_path();
#endif
    std::vector<std::filesystem::path> scan;
    scan.push_back(ed / "include" / "towav");
    scan.push_back(std::filesystem::current_path() / "include" / "towav");

    {
        auto p = ed;
        for(int i=0;i<5;i++){ p = p.parent_path(); if(p.empty()) break; scan.push_back(p / "include" / "towav"); }
    }
    {
        auto p = std::filesystem::current_path();
        for(int i=0;i<5;i++){ p = p.parent_path(); if(p.empty()) break; scan.push_back(p / "include" / "towav"); }
    }
    {
        auto src = std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path();
        scan.push_back(src);
        for(int i=0;i<5;i++){ src = src.parent_path(); if(src.empty()) break; scan.push_back(src / "include" / "towav"); }
    }
    if(auto r = try_in_dirs(scan)) return r;

    if(auto r = which("towav")) return r;

    auto def = std::filesystem::path(DEFAULT_TOWAV_DIR);
    if(auto r = try_in_dirs({def})) return r;

    return std::nullopt;
}

#ifdef _WIN32
static std::wstring utf8_to_wide(const std::string& s){
    if(s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), len);
    return w;
}
#endif

static bool run_towav(const std::string& towav_path, const std::filesystem::path& xma_path, const std::filesystem::path& cwd){
#ifdef _WIN32
    std::wstring app = utf8_to_wide(towav_path);
    std::wstring arg = L" \"" + utf8_to_wide(xma_path.filename().string()) + L"\"";
    std::wstring cmd = L"\"" + app + L"\"" + arg;
    std::wstring wdir = utf8_to_wide(cwd.string());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmdline = cmd;
    BOOL ok = CreateProcessW(app.c_str(), cmdline.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, wdir.empty()? nullptr : wdir.c_str(), &si, &pi);
    if(!ok) return false;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec = 1;
    GetExitCodeProcess(pi.hProcess, &ec);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return ec==0;
#else
    auto old = std::filesystem::current_path();
    std::filesystem::current_path(cwd);
    std::string cmd = "\"" + towav_path + "\" \"" + xma_path.filename().string() + "\"";
    int rc = std::system(cmd.c_str());
    std::filesystem::current_path(old);
    return rc==0;
#endif
}

static bool read_file(const std::filesystem::path& p, std::vector<uint8_t>& out){
    std::ifstream f(p, std::ios::binary);
    if(!f) return false;
    f.seekg(0,std::ios::end); std::streamoff sz=f.tellg(); f.seekg(0,std::ios::beg);
    out.resize((size_t)sz);
    if(sz>0) f.read((char*)out.data(), sz);
    return true;
}

static bool write_file(const std::filesystem::path& p, const std::vector<uint8_t>& data){
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary|std::ios::trunc);
    if(!f) return false;
    if(!data.empty()) f.write((const char*)data.data(), (std::streamsize)data.size());
    return true;
}

static void file_copy(const std::filesystem::path& a, const std::filesystem::path& b){
    std::filesystem::copy_file(a,b,std::filesystem::copy_options::overwrite_existing);
}

static void file_move(const std::filesystem::path& a, const std::filesystem::path& b){
    std::error_code ec;
    std::filesystem::rename(a,b,ec);
    if(ec){ std::filesystem::copy_file(a,b,std::filesystem::copy_options::overwrite_existing,ec); std::filesystem::remove(a,ec); }
}

static void file_remove_if(const std::filesystem::path& p){
    std::error_code ec; std::filesystem::remove(p,ec);
}

static void convert_one(const std::filesystem::path& p, const std::optional<std::filesystem::path>& towav_dir = std::filesystem::path(DEFAULT_TOWAV_DIR), bool keep=false){
    auto out_pcm = p.parent_path() / (p.stem().string() + "_pcm.wav");
    if(std::filesystem::exists(out_pcm)){ return; }
    std::vector<uint8_t> data; if(!read_file(p,data)) return;
    std::filesystem::path repaired_path;
    std::filesystem::path src_for_decode = p;
    if(starts_with_xma_magic(data) && data.size()>4 && has_riff_wave(std::vector<uint8_t>(data.begin()+4,data.end()))){
        repaired_path = p.parent_path() / (p.stem().string() + "._repaired.wav");
        std::vector<uint8_t> body(data.begin()+4,data.end());
        fix_riff_size(body);
        write_file(repaired_path, body);
        src_for_decode = repaired_path;
    }else{
        if(has_riff_wave(data)){
            auto rep = repair_wave(data);
            if(rep.second){
                repaired_path = p.parent_path() / (p.stem().string() + "._repaired.wav");
                write_file(repaired_path, rep.first);
                src_for_decode = repaired_path;
            }else{
                src_for_decode = p;
            }
        }else{
            return;
        }
    }
    auto towav_path = find_towav(towav_dir);
    if(!towav_path){ return; }
    auto work_dir = src_for_decode.parent_path();
    auto temp_xma = work_dir / (src_for_decode.stem().string() + ".xma");
    file_copy(src_for_decode, temp_xma);
    bool ok = run_towav(*towav_path, temp_xma, work_dir);
    file_remove_if(temp_xma);
    if(!ok) return;
    auto produced_wav = work_dir / (temp_xma.stem().string() + ".wav");
    if(std::filesystem::exists(produced_wav)){
        file_move(produced_wav, out_pcm);
    }else{
        auto alt = work_dir / (src_for_decode.stem().string() + ".wav");
        if(std::filesystem::exists(alt)){
            file_move(alt, out_pcm);
        }
    }
    if(!repaired_path.empty() && !keep) file_remove_if(repaired_path);
}

static bool convert_wav_inplace_same_name(const std::filesystem::path& path, const std::optional<std::filesystem::path>& towav_dir = std::filesystem::path(DEFAULT_TOWAV_DIR), bool keep_repaired=false){
    auto p = path;
    if(!std::filesystem::exists(p)){ return false; }
    auto s = p.extension().string(); std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    if(s != ".wav"){ return false; }
    std::vector<uint8_t> data; if(!read_file(p,data)) return false;
    std::filesystem::path repaired_path;
    auto src_for_decode = p;
    if(starts_with_xma_magic(data) && data.size()>4 && has_riff_wave(std::vector<uint8_t>(data.begin()+4,data.end()))){
        repaired_path = p.parent_path() / (p.stem().string() + "._repaired.wav");
        std::vector<uint8_t> body(data.begin()+4,data.end());
        fix_riff_size(body);
        write_file(repaired_path, body);
        src_for_decode = repaired_path;
    }else if(has_riff_wave(data)){
        auto rep = repair_wave(data);
        if(rep.second){
            repaired_path = p.parent_path() / (p.stem().string() + "._repaired.wav");
            write_file(repaired_path, rep.first);
            src_for_decode = repaired_path;
        }
    }else{
        return false;
    }
    auto towav_path = find_towav(towav_dir);
    if(!towav_path){ return false; }
    auto work_dir = src_for_decode.parent_path();
    auto temp_xma = work_dir / (src_for_decode.stem().string() + ".xma");
    file_copy(src_for_decode, temp_xma);
    bool ok = run_towav(*towav_path, temp_xma, work_dir);
    file_remove_if(temp_xma);
    if(!ok) return false;
    auto produced_wav = work_dir / (temp_xma.stem().string() + ".wav");
    if(!std::filesystem::exists(produced_wav)){
        auto alt = work_dir / (src_for_decode.stem().string() + ".wav");
        if(std::filesystem::exists(alt)) produced_wav = alt;
        else { return false; }
    }
    auto bak = p.parent_path() / (p.stem().string() + ".wav.bak");
    std::error_code ec;
    if(std::filesystem::exists(p)) std::filesystem::rename(p,bak,ec);
    file_move(produced_wav, p);
    if(!repaired_path.empty() && !keep_repaired) file_remove_if(repaired_path);
    if(std::filesystem::exists(bak)) file_remove_if(bak);
    return true;
}

static void convert_all_in_dir_inplace(const std::filesystem::path& root){
    for(auto it = std::filesystem::recursive_directory_iterator(root); it != std::filesystem::recursive_directory_iterator(); ++it){
        if(!it->is_regular_file()) continue;
        auto ext = it->path().extension().string(); std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if(ext==".wav"){
            try{ convert_wav_inplace_same_name(it->path()); } catch(...){ }
        }
    }
}

static bool convert_selected_inplace(const std::filesystem::path& file_path){
    return convert_wav_inplace_same_name(file_path);
}
