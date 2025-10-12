#include "Operations.h"
#include "Progress.h"
#include "Utils.h"
#include "Files.h"
#include "BNKCore.cpp"
#include "audio.cpp"
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <optional>
#include "HexView.h"
#include "mdl_converter.h"

void extract_file_one(const std::string &bnk_path, const BNKItemUI &item, const std::string &base_out_dir,
                             bool convert_audio) {
    std::filesystem::create_directories(base_out_dir);
    auto dst = std::filesystem::path(base_out_dir) / item.name;
    std::filesystem::create_directories(dst.parent_path());
    extract_one(bnk_path, item.index, dst.string());
    if (convert_audio && is_audio_file(item.name)) convert_wav_inplace_same_name(dst);
}

void on_extract_selected_raw() {
    int idx = S.selected_file_index;
    if (idx < 0 || idx >= (int) S.files.size()) {
        show_error_box("No file selected.");
        return;
    }

    std::string bnk_to_use;
    if (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty()) {
        bnk_to_use = S.selected_nested_temp_path;
    } else {
        bnk_to_use = S.selected_bnk;
    }

    if (bnk_to_use.empty()) {
        show_error_box("No BNK selected.");
        return;
    }
    auto item = S.files[(size_t) idx];
    auto base_out = (std::filesystem::current_path() / "extracted").string();
    progress_open(1, "Extracting File...");
    progress_update(0, 1, item.name);
    std::thread([item,base_out,bnk_to_use]() {
        if (!S.cancel_requested && !S.exiting) {
            try { extract_file_one(bnk_to_use, item, base_out, false); } catch (...) {
            }
        }
        progress_update(1, 1, item.name);
        progress_done();
        if (!S.cancel_requested) show_completion_box(
            std::string("Extraction complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).string());
        S.cancel_requested = false;
    }).detach();
}

void on_extract_selected_wav() {
    int idx = S.selected_file_index;
    if (idx < 0 || idx >= (int) S.files.size()) {
        show_error_box("No file selected.");
        return;
    }
    if (S.selected_bnk.empty()) {
        show_error_box("No BNK selected.");
        return;
    }
    auto item = S.files[(size_t) idx];
    if (!is_audio_file(item.name)) {
        show_error_box("Selected file is not .wav");
        return;
    }
    auto base_out = (std::filesystem::current_path() / "extracted").string();
    progress_open(1, "Exporting WAV...");
    progress_update(0, 1, item.name);
    std::thread([item,base_out]() {
        if (!S.cancel_requested && !S.exiting) {
            try { extract_file_one(S.selected_bnk, item, base_out, true); } catch (...) {
            }
        }
        progress_update(1, 1, item.name);
        progress_done();
        if (!S.cancel_requested) show_completion_box(
            std::string("WAV export complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).string());
        S.cancel_requested = false;
    }).detach();
}

void on_dump_all_raw() {
    std::string bnk_to_use;
    if (S.selected_nested_index != -1 && !S.selected_nested_temp_path.empty()) {
        bnk_to_use = S.selected_nested_temp_path;
    } else {
        bnk_to_use = S.selected_bnk;
    }

    if (bnk_to_use.empty()) {
        show_error_box("No BNK selected.");
        return;
    }
    if (S.files.empty()) {
        show_error_box("No files to dump in this BNK.");
        return;
    }
    auto base_out = (std::filesystem::current_path() / "extracted").string();
    int total = (int) S.files.size();
    progress_open(total, "Dumping...");
    progress_update(0, total, "Starting...");
    std::thread([base_out,total,bnk_to_use]() {
        std::atomic<int> dumped{0};
        std::mutex fail_m;
        std::vector<std::string> failed;
        auto work = [&](const BNKItemUI &it) {
            if (S.cancel_requested || S.exiting) return;
            try { extract_file_one(bnk_to_use, it, base_out, false); } catch (...) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(it.name);
            }
            int cur = ++dumped;
            progress_update(cur, total, std::filesystem::path(it.name).filename().string());
        };
        if (!S.cancel_requested) {
            std::vector<std::thread> pool;
            int n = std::min(8, std::max(1, (int) std::thread::hardware_concurrency()));
            std::atomic<size_t> i{0};
            for (int t = 0; t < n; ++t) pool.emplace_back([&]() {
                for (;;) {
                    size_t k = i.fetch_add(1);
                    if (k >= S.files.size()) break;
                    work(S.files[k]);
                }
            });
            for (auto &th: pool) th.join();
        }
        progress_done();
        std::string msg = std::string("Dump complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).
                          string();
        if (!failed.empty()) {
            msg += std::string("\nFailed: ") + std::to_string((int) failed.size());
        }
        show_completion_box(msg);
        S.cancel_requested = false;
    }).detach();
}

void on_export_wavs() {
    if (S.selected_bnk.empty()) {
        show_error_box("No BNK selected.");
        return;
    }
    std::vector<BNKItemUI> audio_files;
    for (auto &f: S.files) if (is_audio_file(f.name)) audio_files.push_back(f);
    if (audio_files.empty()) {
        show_error_box("No .wav files in this BNK.");
        return;
    }
    auto base_out = (std::filesystem::current_path() / "extracted").string();
    int total = (int) audio_files.size();
    progress_open(total, "Exporting WAVs...");
    progress_update(0, total, "Starting...");
    std::thread([audio_files,base_out,total]() {
        std::atomic<int> done{0};
        std::mutex fail_m;
        std::vector<std::string> failed;
        auto work = [&](const BNKItemUI &it) {
            if (S.cancel_requested || S.exiting) return;
            try { extract_file_one(S.selected_bnk, it, base_out, true); } catch (...) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(it.name);
            }
            int cur = ++done;
            progress_update(cur, total, std::filesystem::path(it.name).filename().string());
        };
        if (!S.cancel_requested) {
            std::vector<std::thread> pool;
            int n = std::min(4, std::max(1, (int) std::thread::hardware_concurrency() / 2));
            std::atomic<size_t> i{0};
            for (int t = 0; t < n; ++t) pool.emplace_back([&]() {
                for (;;) {
                    size_t k = i.fetch_add(1);
                    if (k >= audio_files.size()) break;
                    work(audio_files[k]);
                }
            });
            for (auto &th: pool) th.join();
        }
        progress_done();
        std::string msg = std::string("WAV export complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).
                          string();
        if (!failed.empty()) {
            msg += std::string("\nFailed: ") + std::to_string((int) failed.size());
        }
        show_completion_box(msg);
        S.cancel_requested = false;
    }).detach();
}

void on_rebuild_and_extract() {
    auto p_headers = find_bnk_by_filename("globals_texture_headers.bnk");
    auto p_mip0 = find_bnk_by_filename("1024mip0_textures.bnk");
    auto p_rest = find_bnk_by_filename("globals_textures.bnk");
    if (!p_headers || !p_rest) {
        show_error_box("Required BNKs not found.");
        return;
    }

    BNKReader r_headers(*p_headers);
    BNKReader r_rest(*p_rest);
    std::optional<BNKReader> r_mip0;
    if (p_mip0) r_mip0.emplace(*p_mip0);

    struct Entry {
        int idx;
        std::string name;
        uint32_t size;
    };
    std::vector<Entry> H, R, M;
    for (size_t i = 0; i < r_headers.list_files().size(); ++i) {
        auto &e = r_headers.list_files()[i];
        H.push_back({(int) i, e.name, e.uncompressed_size});
    }
    for (size_t i = 0; i < r_rest.list_files().size(); ++i) {
        auto &e = r_rest.list_files()[i];
        R.push_back({(int) i, e.name, e.uncompressed_size});
    }
    if (r_mip0) for (size_t i = 0; i < r_mip0->list_files().size(); ++i) {
        auto &e = r_mip0->list_files()[i];
        M.push_back({(int) i, e.name, e.uncompressed_size});
    }

    std::unordered_map<std::string, int> mapH, mapR, mapM;
    mapH.reserve(H.size() * 2 + 1);
    mapR.reserve(R.size() * 2 + 1);
    mapM.reserve(M.size() * 2 + 1);
    for (auto &e: H) {
        std::string fname = std::filesystem::path(e.name).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        mapH.emplace(fname, e.idx);
    }
    for (auto &e: R) {
        std::string fname = std::filesystem::path(e.name).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        mapR.emplace(fname, e.idx);
    }
    for (auto &e: M) {
        std::string fname = std::filesystem::path(e.name).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        mapM.emplace(fname, e.idx);
    }

    std::vector<std::string> names;
    names.reserve(std::max(H.size(), R.size()));
    for (auto &e: H) names.push_back(e.name);
    for (auto &e: R) {
        std::string fname = std::filesystem::path(e.name).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        if (!mapH.count(fname)) names.push_back(e.name);
    }

    int total = (int) names.size();
    if (total <= 0) {
        show_error_box("No texture names found.");
        return;
    }

    auto out_root = (std::filesystem::current_path() / "extracted").string();
    progress_open(total, "Rebuilding...");
    progress_update(0, total, "Starting...");
    std::thread([=]() {
        int done = 0;
        auto tmpdir = std::filesystem::temp_directory_path() / "f2_tex_rebuild";
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);
        for (auto &name: names) {
            if (S.cancel_requested || S.exiting) break;
            std::string fname = std::filesystem::path(name).filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
            if (!mapH.count(fname) || !mapR.count(fname)) {
                progress_update(++done, total, name);
                continue;
            }
            auto out_path = std::filesystem::path(out_root) / name;
            std::filesystem::create_directories(out_path.parent_path(), ec);

            auto tmp_h = tmpdir / ("h_" + std::to_string(done) + ".bin");
            auto tmp_m = tmpdir / ("m_" + std::to_string(done) + ".bin");
            auto tmp_r = tmpdir / ("r_" + std::to_string(done) + ".bin");

            try {
                extract_one(*p_headers, mapH.at(fname), tmp_h.string());
                if (mapM.count(fname) && p_mip0) extract_one(*p_mip0, mapM.at(fname), tmp_m.string());
                extract_one(*p_rest, mapR.at(fname), tmp_r.string());

                std::ofstream out(out_path, std::ios::binary);
                std::ifstream fh(tmp_h, std::ios::binary);
                out << fh.rdbuf();
                if (std::filesystem::exists(tmp_m)) {
                    std::ifstream fm(tmp_m, std::ios::binary);
                    out << fm.rdbuf();
                }
                std::ifstream fr(tmp_r, std::ios::binary);
                out << fr.rdbuf();

                std::filesystem::remove(tmp_h, ec);
                if (std::filesystem::exists(tmp_m)) std::filesystem::remove(tmp_m, ec);
                std::filesystem::remove(tmp_r, ec);
            } catch (...) {
            }
            progress_update(++done, total, name);
        }
        progress_done();
        if (!S.cancel_requested) show_completion_box(
            std::string("Rebuild complete.\n\nOutput folder:\n") + std::filesystem::absolute(out_root).string());
        S.cancel_requested = false;
    }).detach();
}

void on_rebuild_and_extract_models() {
    auto p_headers = find_bnk_by_filename("globals_model_headers.bnk");
    auto p_rest    = find_bnk_by_filename("globals_models.bnk");
    if (!p_headers || !p_rest) {
        show_error_box("Required BNKs not found.");
        return;
    }

    BNKReader r_headers(*p_headers);
    BNKReader r_rest(*p_rest);

    struct Entry { int idx; std::string name; uint32_t size; };
    std::vector<Entry> H, R;
    for (size_t i = 0; i < r_headers.list_files().size(); ++i) {
        auto &e = r_headers.list_files()[i];
        H.push_back({(int)i, e.name, e.uncompressed_size});
    }
    for (size_t i = 0; i < r_rest.list_files().size(); ++i) {
        auto &e = r_rest.list_files()[i];
        R.push_back({(int)i, e.name, e.uncompressed_size});
    }

    std::unordered_map<std::string, int> mapH, mapR;
    mapH.reserve(H.size() * 2 + 1);
    mapR.reserve(R.size() * 2 + 1);
    for (auto &e : H) {
        std::string fname = std::filesystem::path(e.name).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        mapH.emplace(fname, e.idx);
    }
    for (auto &e : R) {
        std::string fname = std::filesystem::path(e.name).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        mapR.emplace(fname, e.idx);
    }

    std::vector<std::string> names;
    names.reserve(std::max(H.size(), R.size()));
    for (auto &e : H) names.push_back(e.name);
    for (auto &e : R) {
        std::string fname = std::filesystem::path(e.name).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        if (!mapH.count(fname)) names.push_back(e.name);
    }

    const int total = (int)names.size();
    if (total <= 0) {
        show_error_box("No model names found.");
        return;
    }

    auto out_root = (std::filesystem::current_path() / "extracted").string();
    progress_open(total, "Rebuilding models...");
    progress_update(0, total, "Starting...");

    std::thread([=]() {
        int done = 0;
        auto tmpdir = std::filesystem::temp_directory_path() / "f2_mdl_rebuild";
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);

        for (auto &name : names) {
            if (S.cancel_requested || S.exiting) break;

            std::string fname = std::filesystem::path(name).filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);

            if (!mapH.count(fname) || !mapR.count(fname)) {
                progress_update(++done, total, name);
                continue;
            }

            auto out_path = std::filesystem::path(out_root) / name;
            std::filesystem::create_directories(out_path.parent_path(), ec);

            auto tmp_h = tmpdir / ("h_" + std::to_string(done) + ".bin");
            auto tmp_r = tmpdir / ("r_" + std::to_string(done) + ".bin");

            try {
                extract_one(*p_headers, mapH.at(fname), tmp_h.string());
                extract_one(*p_rest,    mapR.at(fname), tmp_r.string());

                std::ofstream out(out_path, std::ios::binary);
                { std::ifstream fh(tmp_h, std::ios::binary); out << fh.rdbuf(); }
                { std::ifstream fr(tmp_r, std::ios::binary); out << fr.rdbuf(); }

                std::filesystem::remove(tmp_h, ec);
                std::filesystem::remove(tmp_r, ec);
            } catch (...) {}

            progress_update(++done, total, name);
        }

        progress_done();
        if (!S.cancel_requested)
            show_completion_box(std::string("Model rebuild complete.\n\nOutput folder:\n") + std::filesystem::absolute(out_root).string());
        S.cancel_requested = false;
    }).detach();
}

void on_rebuild_and_extract_one(const std::string &tex_name) {
    auto p_headers = find_bnk_by_filename("globals_texture_headers.bnk");
    auto p_mip0 = find_bnk_by_filename("1024mip0_textures.bnk");
    auto p_rest = find_bnk_by_filename("globals_textures.bnk");
    if (!p_headers || !p_rest) {
        show_error_box("Required BNKs not found.");
        return;
    }

    BNKReader r_headers(*p_headers);
    BNKReader r_rest(*p_rest);
    std::optional<BNKReader> r_mip0;
    if (p_mip0) r_mip0.emplace(*p_mip0);

    std::unordered_map<std::string, int> mapH, mapR, mapM;
    for (size_t i = 0; i < r_headers.list_files().size(); ++i) {
        auto &e = r_headers.list_files()[i];
        std::string fname = std::filesystem::path(e.name).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        mapH.emplace(fname, (int) i);
    }
    for (size_t i = 0; i < r_rest.list_files().size(); ++i) {
        auto &e = r_rest.list_files()[i];
        std::string fname = std::filesystem::path(e.name).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        mapR.emplace(fname, (int) i);
    }
    if (r_mip0)
        for (size_t i = 0; i < r_mip0->list_files().size(); ++i) {
            auto &e = r_mip0->list_files()[i];
            std::string fname = std::filesystem::path(e.name).filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
            mapM.emplace(fname, (int) i);
        }

    std::string fname = std::filesystem::path(tex_name).filename().string();
    std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
    if (!mapH.count(fname) || !mapR.count(fname)) {
        show_error_box("Texture not found in required BNKs.");
        return;
    }

    auto out_root = (std::filesystem::current_path() / "extracted").string();
    progress_open(1, "Rebuilding...");
    progress_update(0, 1, tex_name);
    std::thread([=]() {
        auto tmpdir = std::filesystem::temp_directory_path() / "f2_tex_rebuild_one";
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);
        auto out_path = std::filesystem::path(out_root) / tex_name;
        std::filesystem::create_directories(out_path.parent_path(), ec);
        auto tmp_h = tmpdir / "h.bin";
        auto tmp_m = tmpdir / "m.bin";
        auto tmp_r = tmpdir / "r.bin";
        try {
            extract_one(*p_headers, mapH.at(fname), tmp_h.string());
            if (mapM.count(fname) && p_mip0) extract_one(*p_mip0, mapM.at(fname), tmp_m.string());
            extract_one(*p_rest, mapR.at(fname), tmp_r.string());
            std::ofstream out(out_path, std::ios::binary);
            std::ifstream fh(tmp_h, std::ios::binary);
            out << fh.rdbuf();
            if (std::filesystem::exists(tmp_m)) {
                std::ifstream fm(tmp_m, std::ios::binary);
                out << fm.rdbuf();
            }
            std::ifstream fr(tmp_r, std::ios::binary);
            out << fr.rdbuf();
            std::filesystem::remove(tmp_h, ec);
            if (std::filesystem::exists(tmp_m)) std::filesystem::remove(tmp_m, ec);
            std::filesystem::remove(tmp_r, ec);
        } catch (...) {
        }
        progress_update(1, 1, tex_name);
        progress_done();
        if (!S.cancel_requested) show_completion_box(
            std::string("Rebuild complete.\n\nOutput folder:\n") + std::filesystem::absolute(out_root).string());
        S.cancel_requested = false;
    }).detach();
}

void on_rebuild_and_extract_one_mdl(const std::string &mdl_name) {
    auto p_headers = find_bnk_by_filename("globals_model_headers.bnk");
    auto p_rest    = find_bnk_by_filename("globals_models.bnk");
    if (!p_headers || !p_rest) {
        show_error_box("Required BNKs not found.");
        return;
    }

    BNKReader r_headers(*p_headers);
    BNKReader r_rest(*p_rest);

    std::unordered_map<std::string, int> mapH, mapR;
    for (size_t i = 0; i < r_headers.list_files().size(); ++i) {
        auto &e = r_headers.list_files()[i];
        std::string fname = std::filesystem::path(e.name).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        mapH.emplace(fname, (int)i);
    }
    for (size_t i = 0; i < r_rest.list_files().size(); ++i) {
        auto &e = r_rest.list_files()[i];
        std::string fname = std::filesystem::path(e.name).filename().string();
        std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
        mapR.emplace(fname, (int)i);
    }

    std::string key = std::filesystem::path(mdl_name).filename().string();
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);

    if (!mapH.count(key) || !mapR.count(key)) {
        show_error_box("Model not found in required BNKs.");
        return;
    }

    auto out_root = (std::filesystem::current_path() / "extracted").string();
    progress_open(1, "Rebuilding model...");
    progress_update(0, 1, mdl_name);

    std::thread([=]() {
        auto tmpdir = std::filesystem::temp_directory_path() / "f2_mdl_rebuild_one";
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);

        auto out_path = std::filesystem::path(out_root) / mdl_name;
        std::filesystem::create_directories(out_path.parent_path(), ec);

        auto tmp_h = tmpdir / "h.bin";
        auto tmp_r = tmpdir / "r.bin";

        try {
            extract_one(*p_headers, mapH.at(key), tmp_h.string());
            extract_one(*p_rest,    mapR.at(key), tmp_r.string());

            std::ofstream out(out_path, std::ios::binary);
            { std::ifstream fh(tmp_h, std::ios::binary); out << fh.rdbuf(); }
            { std::ifstream fr(tmp_r, std::ios::binary); out << fr.rdbuf(); }

            std::filesystem::remove(tmp_h, ec);
            std::filesystem::remove(tmp_r, ec);
        } catch (...) {}

        progress_update(1, 1, mdl_name);
        progress_done();
        if (!S.cancel_requested)
            show_completion_box(std::string("Model rebuild complete.\n\nOutput folder:\n") + std::filesystem::absolute(out_root).string());
        S.cancel_requested = false;
    }).detach();
}

void on_dump_all_global(const std::vector<GlobalHit>& hits) {
    if (hits.empty()) {
        show_error_box("No files to dump.");
        return;
    }

    auto base_out = (std::filesystem::current_path() / "extracted").string();
    int total = (int)hits.size();
    progress_open(total, "Dumping...");
    progress_update(0, total, "Starting...");

    std::thread([hits, base_out, total]() {
        std::atomic<int> dumped{0};
        std::mutex fail_m;
        std::vector<std::string> failed;

        auto work = [&](const GlobalHit &h) {
            if (S.cancel_requested || S.exiting) return;
            try {
                BNKItemUI item;
                item.index = h.index;
                item.name = h.file_name;
                item.size = h.size;
                extract_file_one(h.bnk_path, item, base_out, false);
            } catch (...) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(h.file_name);
            }
            int cur = ++dumped;
            progress_update(cur, total, std::filesystem::path(h.file_name).filename().string());
        };

        if (!S.cancel_requested) {
            std::vector<std::thread> pool;
            int n = std::min(8, std::max(1, (int)std::thread::hardware_concurrency()));
            std::atomic<size_t> i{0};
            for (int t = 0; t < n; ++t) pool.emplace_back([&]() {
                for (;;) {
                    size_t k = i.fetch_add(1);
                    if (k >= hits.size()) break;
                    work(hits[k]);
                }
            });
            for (auto &th: pool) th.join();
        }

        progress_done();
        std::string msg = std::string("Dump complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).string();
        if (!failed.empty()) {
            msg += std::string("\nFailed: ") + std::to_string((int)failed.size());
        }
        show_completion_box(msg);
        S.cancel_requested = false;
    }).detach();
}

void on_export_wavs_global(const std::vector<GlobalHit>& hits) {
    std::vector<GlobalHit> audio_files;
    for (auto &h: hits) if (is_audio_file(h.file_name)) audio_files.push_back(h);

    if (audio_files.empty()) {
        show_error_box("No .wav files in filtered results.");
        return;
    }

    auto base_out = (std::filesystem::current_path() / "extracted").string();
    int total = (int)audio_files.size();
    progress_open(total, "Exporting WAVs...");
    progress_update(0, total, "Starting...");

    std::thread([audio_files, base_out, total]() {
        std::atomic<int> done{0};
        std::mutex fail_m;
        std::vector<std::string> failed;

        auto work = [&](const GlobalHit &h) {
            if (S.cancel_requested || S.exiting) return;
            try {
                BNKItemUI item;
                item.index = h.index;
                item.name = h.file_name;
                item.size = h.size;
                extract_file_one(h.bnk_path, item, base_out, true);
            } catch (...) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(h.file_name);
            }
            int cur = ++done;
            progress_update(cur, total, std::filesystem::path(h.file_name).filename().string());
        };

        if (!S.cancel_requested) {
            std::vector<std::thread> pool;
            int n = std::min(4, std::max(1, (int)std::thread::hardware_concurrency() / 2));
            std::atomic<size_t> i{0};
            for (int t = 0; t < n; ++t) pool.emplace_back([&]() {
                for (;;) {
                    size_t k = i.fetch_add(1);
                    if (k >= audio_files.size()) break;
                    work(audio_files[k]);
                }
            });
            for (auto &th: pool) th.join();
        }

        progress_done();
        std::string msg = std::string("WAV export complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).string();
        if (!failed.empty()) {
            msg += std::string("\nFailed: ") + std::to_string((int)failed.size());
        }
        show_completion_box(msg);
        S.cancel_requested = false;
    }).detach();
}

void on_rebuild_and_extract_global_tex(const std::vector<GlobalHit>& hits) {
    auto p_headers = find_bnk_by_filename("globals_texture_headers.bnk");
    auto p_mip0 = find_bnk_by_filename("1024mip0_textures.bnk");
    auto p_rest = find_bnk_by_filename("globals_textures.bnk");
    if (!p_headers || !p_rest) {
        show_error_box("Required BNKs not found.");
        return;
    }

    std::vector<GlobalHit> tex_files;
    for (auto &h: hits) if (is_tex_file(h.file_name)) tex_files.push_back(h);

    if (tex_files.empty()) {
        show_error_box("No .tex files in filtered results.");
        return;
    }

    auto out_root = (std::filesystem::current_path() / "extracted").string();
    int total = (int)tex_files.size();
    progress_open(total, "Rebuilding...");
    progress_update(0, total, "Starting...");

    std::thread([tex_files, out_root, total, p_headers, p_mip0, p_rest]() {
        BNKReader r_headers(*p_headers);
        BNKReader r_rest(*p_rest);
        std::optional<BNKReader> r_mip0;
        if (p_mip0) r_mip0.emplace(*p_mip0);

        std::unordered_map<std::string, int> mapH, mapR, mapM;
        for (size_t i = 0; i < r_headers.list_files().size(); ++i) {
            auto &e = r_headers.list_files()[i];
            std::string fname = std::filesystem::path(e.name).filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
            mapH.emplace(fname, (int)i);
        }
        for (size_t i = 0; i < r_rest.list_files().size(); ++i) {
            auto &e = r_rest.list_files()[i];
            std::string fname = std::filesystem::path(e.name).filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
            mapR.emplace(fname, (int)i);
        }
        if (r_mip0) {
            for (size_t i = 0; i < r_mip0->list_files().size(); ++i) {
                auto &e = r_mip0->list_files()[i];
                std::string fname = std::filesystem::path(e.name).filename().string();
                std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
                mapM.emplace(fname, (int)i);
            }
        }

        int done = 0;
        auto tmpdir = std::filesystem::temp_directory_path() / "f2_tex_rebuild_global";
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);

        for (auto &h : tex_files) {
            if (S.cancel_requested || S.exiting) break;

            std::string fname = std::filesystem::path(h.file_name).filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);

            if (!mapH.count(fname) || !mapR.count(fname)) {
                progress_update(++done, total, h.file_name);
                continue;
            }

            auto out_path = std::filesystem::path(out_root) / h.file_name;
            std::filesystem::create_directories(out_path.parent_path(), ec);

            auto tmp_h = tmpdir / ("h_" + std::to_string(done) + ".bin");
            auto tmp_m = tmpdir / ("m_" + std::to_string(done) + ".bin");
            auto tmp_r = tmpdir / ("r_" + std::to_string(done) + ".bin");

            try {
                extract_one(*p_headers, mapH.at(fname), tmp_h.string());
                if (mapM.count(fname) && p_mip0) extract_one(*p_mip0, mapM.at(fname), tmp_m.string());
                extract_one(*p_rest, mapR.at(fname), tmp_r.string());

                std::ofstream out(out_path, std::ios::binary);
                std::ifstream fh(tmp_h, std::ios::binary);
                out << fh.rdbuf();
                if (std::filesystem::exists(tmp_m)) {
                    std::ifstream fm(tmp_m, std::ios::binary);
                    out << fm.rdbuf();
                }
                std::ifstream fr(tmp_r, std::ios::binary);
                out << fr.rdbuf();

                std::filesystem::remove(tmp_h, ec);
                if (std::filesystem::exists(tmp_m)) std::filesystem::remove(tmp_m, ec);
                std::filesystem::remove(tmp_r, ec);
            } catch (...) {}

            progress_update(++done, total, h.file_name);
        }

        progress_done();
        if (!S.cancel_requested)
            show_completion_box(std::string("Rebuild complete.\n\nOutput folder:\n") + std::filesystem::absolute(out_root).string());
        S.cancel_requested = false;
    }).detach();
}

void on_rebuild_and_extract_global_mdl(const std::vector<GlobalHit>& hits) {
    auto p_headers = find_bnk_by_filename("globals_model_headers.bnk");
    auto p_rest = find_bnk_by_filename("globals_models.bnk");
    if (!p_headers || !p_rest) {
        show_error_box("Required BNKs not found.");
        return;
    }

    std::vector<GlobalHit> mdl_files;
    for (auto &h: hits) if (is_mdl_file(h.file_name)) mdl_files.push_back(h);

    if (mdl_files.empty()) {
        show_error_box("No .mdl files in filtered results.");
        return;
    }

    auto out_root = (std::filesystem::current_path() / "extracted").string();
    int total = (int)mdl_files.size();
    progress_open(total, "Rebuilding models...");
    progress_update(0, total, "Starting...");

    std::thread([mdl_files, out_root, total, p_headers, p_rest]() {
        BNKReader r_headers(*p_headers);
        BNKReader r_rest(*p_rest);

        std::unordered_map<std::string, int> mapH, mapR;
        for (size_t i = 0; i < r_headers.list_files().size(); ++i) {
            auto &e = r_headers.list_files()[i];
            std::string fname = std::filesystem::path(e.name).filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
            mapH.emplace(fname, (int)i);
        }
        for (size_t i = 0; i < r_rest.list_files().size(); ++i) {
            auto &e = r_rest.list_files()[i];
            std::string fname = std::filesystem::path(e.name).filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);
            mapR.emplace(fname, (int)i);
        }

        int done = 0;
        auto tmpdir = std::filesystem::temp_directory_path() / "f2_mdl_rebuild_global";
        std::error_code ec;
        std::filesystem::create_directories(tmpdir, ec);

        for (auto &h : mdl_files) {
            if (S.cancel_requested || S.exiting) break;

            std::string fname = std::filesystem::path(h.file_name).filename().string();
            std::transform(fname.begin(), fname.end(), fname.begin(), ::tolower);

            if (!mapH.count(fname) || !mapR.count(fname)) {
                progress_update(++done, total, h.file_name);
                continue;
            }

            auto out_path = std::filesystem::path(out_root) / h.file_name;
            std::filesystem::create_directories(out_path.parent_path(), ec);

            auto tmp_h = tmpdir / ("h_" + std::to_string(done) + ".bin");
            auto tmp_r = tmpdir / ("r_" + std::to_string(done) + ".bin");

            try {
                extract_one(*p_headers, mapH.at(fname), tmp_h.string());
                extract_one(*p_rest, mapR.at(fname), tmp_r.string());

                std::ofstream out(out_path, std::ios::binary);
                { std::ifstream fh(tmp_h, std::ios::binary); out << fh.rdbuf(); }
                { std::ifstream fr(tmp_r, std::ios::binary); out << fr.rdbuf(); }

                std::filesystem::remove(tmp_h, ec);
                std::filesystem::remove(tmp_r, ec);
            } catch (...) {}

            progress_update(++done, total, h.file_name);
        }

        progress_done();
        if (!S.cancel_requested)
            show_completion_box(std::string("Model rebuild complete.\n\nOutput folder:\n") + std::filesystem::absolute(out_root).string());
        S.cancel_requested = false;
    }).detach();
}

// operations.cpp - Add these functions at the end of the file
void on_extract_adb_selected() {
    int idx = S.selected_file_index;
    if (idx < 0 || idx >= (int)S.files.size()) {
        show_error_box("No file selected.");
        return;
    }
    if (!S.viewing_adb) {
        show_error_box("Not viewing Audio Database.");
        return;
    }

    auto item = S.files[(size_t)idx];
    auto base_out = (std::filesystem::current_path() / "extracted" / "audio_database").string();

    progress_open(1, "Extracting ADB...");
    progress_update(0, 1, item.name);

    std::thread([item, base_out]() {
        if (!S.cancel_requested && !S.exiting) {
            try {
                std::filesystem::create_directories(base_out);
                auto entries = decompress_adb(item.name);

                for (const auto& entry : entries) {
                    auto output_path = std::filesystem::path(base_out) / entry.name;
                    std::ofstream out(output_path, std::ios::binary);
                    out.write((char*)entry.data.data(), entry.data.size());
                }
            } catch (...) {}
        }
        progress_update(1, 1, item.name);
        progress_done();
        if (!S.cancel_requested) show_completion_box(
            std::string("ADB extraction complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).string());
        S.cancel_requested = false;
    }).detach();
}

void on_extract_all_adb() {
    if (!S.viewing_adb) {
        show_error_box("Not viewing Audio Database.");
        return;
    }
    if (S.files.empty()) {
        show_error_box("No ADB files to extract.");
        return;
    }

    auto base_out = (std::filesystem::current_path() / "extracted" / "audio_database").string();
    int total = (int)S.files.size();
    progress_open(total, "Extracting ADB files...");
    progress_update(0, total, "Starting...");

    std::thread([base_out, total]() {
        std::atomic<int> extracted{0};
        std::mutex fail_m;
        std::vector<std::string> failed;

        auto work = [&](const BNKItemUI &it) {
            if (S.cancel_requested || S.exiting) return;
            try {
                std::filesystem::create_directories(base_out);
                auto entries = decompress_adb(it.name);

                for (const auto& entry : entries) {
                    auto output_path = std::filesystem::path(base_out) / entry.name;
                    std::ofstream out(output_path, std::ios::binary);
                    out.write((char*)entry.data.data(), entry.data.size());
                }
            } catch (...) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(it.name);
            }
            int cur = ++extracted;
            progress_update(cur, total, std::filesystem::path(it.name).filename().string());
        };

        if (!S.cancel_requested) {
            std::vector<std::thread> pool;
            int n = std::min(4, std::max(1, (int)std::thread::hardware_concurrency() / 2));
            std::atomic<size_t> i{0};
            for (int t = 0; t < n; ++t) pool.emplace_back([&]() {
                for (;;) {
                    size_t k = i.fetch_add(1);
                    if (k >= S.files.size()) break;
                    work(S.files[k]);
                }
            });
            for (auto &th: pool) th.join();
        }

        progress_done();
        std::string msg = std::string("ADB extraction complete.\n\nOutput folder:\n") + std::filesystem::absolute(base_out).string();
        if (!failed.empty()) {
            msg += std::string("\nFailed: ") + std::to_string((int)failed.size());
        }
        show_completion_box(msg);
        S.cancel_requested = false;
    }).detach();
}

void on_export_mdl_to_glb() {
    int idx = S.selected_file_index;
    if (idx < 0 || idx >= (int)S.files.size()) {
        show_error_box("No file selected.");
        return;
    }

    auto item = S.files[(size_t)idx];
    std::string name = item.name;
    std::string name_lower = name;
    std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

    if (name_lower.size() < 4 || name_lower.substr(name_lower.size() - 4) != ".mdl") {
        show_error_box("Selected file is not .mdl");
        return;
    }

    auto base_out = (std::filesystem::current_path() / "exported_glb").string();
    progress_open(1, "Exporting GLB...");
    progress_update(0, 1, name);

    std::thread([item, name, base_out]() {
        if (!S.cancel_requested && !S.exiting) {
            try {
                std::vector<unsigned char> mdl_buf;
                if (!build_mdl_buffer_for_name(name, mdl_buf)) {
                    progress_done();
                    show_error_box("Failed to build MDL buffer");
                    return;
                }

                std::string out_name = std::filesystem::path(name).stem().string() + ".glb";
                auto out_path = std::filesystem::path(base_out) / out_name;
                std::filesystem::create_directories(out_path.parent_path());

                std::string err;
                if (!mdl_to_glb_full(mdl_buf, out_path.string(), err)) {
                    progress_done();
                    show_error_box("GLB export failed: " + err);
                    return;
                }
            } catch (...) {
                progress_done();
                show_error_box("Exception during export");
                return;
            }
        }
        progress_update(1, 1, name);
        progress_done();
        if (!S.cancel_requested) {
            show_completion_box(
                std::string("GLB export complete.\n\nOutput folder:\n") +
                std::filesystem::absolute(base_out).string());
        }
        S.cancel_requested = false;
    }).detach();
}

void on_export_all_mdl_to_glb() {
    std::vector<BNKItemUI> mdl_files;
    for (auto &f: S.files) {
        std::string name_lower = f.name;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
        if (name_lower.size() >= 4 && name_lower.substr(name_lower.size() - 4) == ".mdl") {
            mdl_files.push_back(f);
        }
    }

    if (mdl_files.empty()) {
        show_error_box("No .mdl files in this BNK.");
        return;
    }

    auto base_out = (std::filesystem::current_path() / "exported_glb").string();
    int total = (int)mdl_files.size();
    progress_open(total, "Exporting GLBs...");
    progress_update(0, total, "Starting...");

    std::thread([mdl_files, base_out, total]() {
        std::atomic<int> done{0};
        std::mutex fail_m;
        std::vector<std::string> failed;

        auto work = [&](const BNKItemUI &it) {
            if (S.cancel_requested || S.exiting) return;
            try {
                std::vector<unsigned char> mdl_buf;
                if (!build_mdl_buffer_for_name(it.name, mdl_buf)) {
                    std::lock_guard<std::mutex> lk(fail_m);
                    failed.push_back(it.name);
                    return;
                }

                std::string out_name = std::filesystem::path(it.name).stem().string() + ".glb";
                auto out_path = std::filesystem::path(base_out) / out_name;
                std::filesystem::create_directories(out_path.parent_path());

                std::string err;
                if (!mdl_to_glb_full(mdl_buf, out_path.string(), err)) {
                    std::lock_guard<std::mutex> lk(fail_m);
                    failed.push_back(it.name);
                }
            } catch (...) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(it.name);
            }
            int cur = ++done;
            progress_update(cur, total, std::filesystem::path(it.name).filename().string());
        };

        if (!S.cancel_requested) {
            std::vector<std::thread> pool;
            int n = std::min(4, std::max(1, (int)std::thread::hardware_concurrency() / 2));
            std::atomic<size_t> i{0};
            for (int t = 0; t < n; ++t) pool.emplace_back([&]() {
                for (;;) {
                    size_t k = i.fetch_add(1);
                    if (k >= mdl_files.size()) break;
                    work(mdl_files[k]);
                }
            });
            for (auto &th: pool) th.join();
        }

        progress_done();
        std::string msg = std::string("GLB export complete.\n\nOutput folder:\n") +
                         std::filesystem::absolute(base_out).string();
        if (!failed.empty()) {
            msg += std::string("\nFailed: ") + std::to_string((int)failed.size());
        }
        show_completion_box(msg);
        S.cancel_requested = false;
    }).detach();
}

void on_export_global_mdl_to_glb(const std::vector<GlobalHit>& hits) {
    std::vector<GlobalHit> mdl_files;
    for (auto &h: hits) {
        std::string name_lower = h.file_name;
        std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);
        if (name_lower.size() >= 4 && name_lower.substr(name_lower.size() - 4) == ".mdl") {
            mdl_files.push_back(h);
        }
    }

    if (mdl_files.empty()) {
        show_error_box("No .mdl files in filtered results.");
        return;
    }

    auto base_out = (std::filesystem::current_path() / "exported_glb").string();
    int total = (int)mdl_files.size();
    progress_open(total, "Exporting GLBs...");
    progress_update(0, total, "Starting...");

    std::thread([mdl_files, base_out, total]() {
        std::atomic<int> done{0};
        std::mutex fail_m;
        std::vector<std::string> failed;

        auto work = [&](const GlobalHit &h) {
            if (S.cancel_requested || S.exiting) return;
            try {
                std::vector<unsigned char> mdl_buf;
                if (!build_mdl_buffer_for_name(h.file_name, mdl_buf)) {
                    std::lock_guard<std::mutex> lk(fail_m);
                    failed.push_back(h.file_name);
                    return;
                }

                std::string out_name = std::filesystem::path(h.file_name).stem().string() + ".glb";
                auto out_path = std::filesystem::path(base_out) / out_name;
                std::filesystem::create_directories(out_path.parent_path());

                std::string err;
                if (!mdl_to_glb_full(mdl_buf, out_path.string(), err)) {
                    std::lock_guard<std::mutex> lk(fail_m);
                    failed.push_back(h.file_name);
                }
            } catch (...) {
                std::lock_guard<std::mutex> lk(fail_m);
                failed.push_back(h.file_name);
            }
            int cur = ++done;
            progress_update(cur, total, std::filesystem::path(h.file_name).filename().string());
        };

        if (!S.cancel_requested) {
            std::vector<std::thread> pool;
            int n = std::min(4, std::max(1, (int)std::thread::hardware_concurrency() / 2));
            std::atomic<size_t> i{0};
            for (int t = 0; t < n; ++t) pool.emplace_back([&]() {
                for (;;) {
                    size_t k = i.fetch_add(1);
                    if (k >= mdl_files.size()) break;
                    work(mdl_files[k]);
                }
            });
            for (auto &th: pool) th.join();
        }

        progress_done();
        std::string msg = std::string("GLB export complete.\n\nOutput folder:\n") +
                         std::filesystem::absolute(base_out).string();
        if (!failed.empty()) {
            msg += std::string("\nFailed: ") + std::to_string((int)failed.size());
        }
        show_completion_box(msg);
        S.cancel_requested = false;
    }).detach();
}