static void asset_export_audio_encoded(const std::string& bnk_path,
                                       int file_index,
                                       const std::string& file_name,
                                       bool aac )
{
    const char* fmt_label = aac ? "AAC"  : "MP3";
    const char* fmt_ext   = aac ? ".m4a" : ".mp3";

    auto out = build_export_target(file_name);
    out.replace_extension(fmt_ext);
    auto scratch = out;
    scratch += ".xma.tmp";

    std::error_code ec;
    if (auto parent = out.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            OutputLog::error(std::string(fmt_label) +
                             " export: cannot create " +
                             parent.string() + " - " + ec.message());
            return;
        }
    }

    bool ok = false;
    try {

        extract_one(bnk_path, file_index, scratch.string());
        if (!std::filesystem::exists(scratch, ec) || ec) {
            throw std::runtime_error("extract_one produced no file");
        }
        auto raw = read_all_bytes(scratch);
        std::filesystem::remove(scratch, ec);
        if (raw.empty()) {
            throw std::runtime_error("extracted .wav is empty");
        }

        std::vector<uint8_t> src(raw.begin(), raw.end());
        std::vector<int16_t> pcm;
        int sr = 0, ch = 0;
        std::string err;
        if (!XmaDecoder::decode_xma_to_pcm(src, pcm, sr, ch, &err) ||
            pcm.empty()) {
            throw std::runtime_error(
                std::string("XMA->PCM decode failed: ") + err);
        }

        bool encoded = aac
            ? MfAudio::encode_pcm_to_aac(pcm, sr, ch, out.string(), &err)
            : MfAudio::encode_pcm_to_mp3(pcm, sr, ch, out.string(), &err);
        if (!encoded) {
            throw std::runtime_error(
                std::string(fmt_label) + " encode failed: " + err);
        }
        ok = true;
    } catch (const std::exception& ex) {
        std::filesystem::remove(scratch, ec);
        OutputLog::error(std::string(fmt_label) + " export exception on "
                         + file_name + ": " + ex.what());
    } catch (...) {
        std::filesystem::remove(scratch, ec);
        OutputLog::error(std::string(fmt_label) + " export exception on "
                         + file_name);
    }

    if (ok) {
        OutputLog::success(std::string("Exported ") +
                           std::filesystem::path(file_name).filename().string()
                           + " as " + fmt_label + " -> " + out.string());
    } else {
        OutputLog::error(std::string(fmt_label) + " export failed: " +
                         file_name);
    }
}
