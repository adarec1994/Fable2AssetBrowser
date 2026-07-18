void dump_wav_files_as(AudioExportFormat fmt) {
    if (fmt == AudioExportFormat::WAV_RAW) {

        dump_wav_files();
        return;
    }

    if (S.all_wav_files.empty()) {
        OutputLog::warn("Dump WAV (PCM/MP3/AAC): no .wav files indexed "
                        "(open a Fable 2 root first).");
        return;
    }

    std::vector<FlatAssetEntry> targets = S.all_wav_files;

    std::unordered_map<std::string, std::vector<int>> by_bnk;
    by_bnk.reserve(64);
    for (size_t i = 0; i < targets.size(); ++i) {
        by_bnk[targets[i].bnk_path].push_back((int)i);
    }
    const int total = (int)targets.size();

    const std::string export_root =
        S.export_dir.empty() ? std::filesystem::absolute("extracted").string()
                             : S.export_dir;

    const char* fmt_label =
        (fmt == AudioExportFormat::WAV_PCM) ? "PCM"  :
        (fmt == AudioExportFormat::MP3)     ? "MP3"  :
        (fmt == AudioExportFormat::AAC)     ? "AAC"  : "?";
    const char* fmt_ext =
        (fmt == AudioExportFormat::WAV_PCM) ? ".wav" :
        (fmt == AudioExportFormat::MP3)     ? ".mp3" :
        (fmt == AudioExportFormat::AAC)     ? ".m4a" : ".bin";

    OutputLog::info(std::string("Exporting ") + std::to_string(total) +
                    " .wav file(s) as " + fmt_label + " -> " + export_root);
    progress_open(total,
                  std::string("Exporting WAVs as ") + fmt_label +
                  " -> " + export_root);
    progress_update(0, total, "Starting...");

    std::thread([targets = std::move(targets),
                 by_bnk = std::move(by_bnk),
                 total, fmt, fmt_label, fmt_ext]() {
        struct PG {
            ~PG() { progress_done(); }
        } pg;

        std::atomic<int> done{0};
        std::vector<std::string> failed;
        std::mutex fail_m;

        try {
            for (const auto& [bnk_path, indices] : by_bnk) {
                if (S.cancel_requested.load() || S.exiting.load()) break;

                for (int ti : indices) {
                    if (S.cancel_requested.load() || S.exiting.load()) break;
                    const auto& e = targets[(size_t)ti];

                    auto out_final = build_asset_out_path(e, fmt_ext);

                    auto out_scratch = out_final;
                    out_scratch += ".xma.tmp";

                    bool ok = false;
                    try {
                        std::error_code ec;
                        if (auto parent = out_final.parent_path();
                            !parent.empty()) {
                            std::filesystem::create_directories(parent, ec);
                        }

                        extract_one(bnk_path, e.file_index,
                                    out_scratch.string());
                        if (!std::filesystem::exists(out_scratch, ec) || ec) {
                            throw std::runtime_error(
                                "extract_one produced no file");
                        }

                        if (fmt == AudioExportFormat::WAV_PCM) {

                            auto raw = read_all_bytes(out_scratch);
                            std::filesystem::remove(out_scratch, ec);
                            if (raw.empty()) {
                                throw std::runtime_error(
                                    "extracted .wav is empty");
                            }
                            std::vector<uint8_t> src(raw.begin(), raw.end());
                            std::string err;
                            if (!XmaDecoder::decode_xma_wav_file_to_pcm_wav(
                                    src, out_final.string(), &err)) {

                                std::ofstream f(out_final, std::ios::binary |
                                                          std::ios::trunc);
                                if (f) {
                                    f.write((const char*)raw.data(),
                                            (std::streamsize)raw.size());
                                }
                                OutputLog::warn(std::string(
                                    "PCM decode failed for ") + e.full_path +
                                    ": " + err + " - kept raw bytes.");
                            }
                            ok = true;
                        } else {

                            auto raw = read_all_bytes(out_scratch);
                            std::filesystem::remove(out_scratch, ec);
                            if (raw.empty()) {
                                throw std::runtime_error(
                                    "extracted .wav is empty");
                            }
                            std::vector<uint8_t> src(raw.begin(), raw.end());
                            std::vector<int16_t> pcm;
                            int sr = 0, ch = 0;
                            std::string err;
                            if (!XmaDecoder::decode_xma_to_pcm(
                                    src, pcm, sr, ch, &err) || pcm.empty()) {
                                throw std::runtime_error(
                                    std::string("XMA->PCM decode failed: ")
                                    + err);
                            }
                            bool encoded = false;
                            if (fmt == AudioExportFormat::MP3) {
                                encoded = MfAudio::encode_pcm_to_mp3(
                                    pcm, sr, ch, out_final.string(), &err);
                            } else {
                                encoded = MfAudio::encode_pcm_to_aac(
                                    pcm, sr, ch, out_final.string(), &err);
                            }
                            if (!encoded) {
                                throw std::runtime_error(
                                    std::string(fmt_label) +
                                    " encode failed: " + err);
                            }
                            ok = true;
                        }
                    } catch (const std::exception& ex) {
                        std::error_code rmec;
                        std::filesystem::remove(out_scratch, rmec);
                        OutputLog::error(std::string("WAV ") + fmt_label +
                                         " exception on " + e.full_path +
                                         ": " + ex.what());
                    } catch (...) {
                        std::error_code rmec;
                        std::filesystem::remove(out_scratch, rmec);
                        OutputLog::error(std::string("WAV ") + fmt_label +
                                         " exception on " + e.full_path);
                    }

                    if (!ok) {
                        std::lock_guard<std::mutex> lk(fail_m);
                        failed.push_back(e.full_path);
                    }
                    int cur = ++done;
                    progress_update(cur, total,
                                    std::filesystem::path(e.name)
                                        .filename().string());
                }
            }
        } catch (const std::exception& ex) {
            OutputLog::error(std::string("WAV ") + fmt_label +
                             " dump worker aborted: " + ex.what());
            return;
        } catch (...) {
            OutputLog::error(std::string("WAV ") + fmt_label +
                             " dump worker aborted (unknown).");
            return;
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn(std::string("WAV ") + fmt_label +
                            " dump cancelled (" +
                            std::to_string(done.load()) + "/" +
                            std::to_string(total) + ").");
            S.cancel_requested = false;
            return;
        }
        const int n_failed = (int)failed.size();
        if (n_failed > 0) {
            OutputLog::warn(std::string("WAV ") + fmt_label +
                            " dump finished: " +
                            std::to_string(done.load() - n_failed) + "/" +
                            std::to_string(total) + " written, " +
                            std::to_string(n_failed) + " failed.");
        } else {
            OutputLog::success(std::string("WAV ") + fmt_label +
                               " dump complete: " +
                               std::to_string(total) + " files written.");
        }
    }).detach();
}
