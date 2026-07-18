float clip_duration_seconds(const AnimClip& clip) {
    if (clip.fps <= 0.0f) return 0.0f;
    uint32_t frames = clip.toc_frame_count;
    if (global_data_file().is_open()) {
        auto h = global_data_file().parse_clip_header(clip);
        if (h.ok && h.frame_count != 0) {
            frames = h.frame_count;
        }
    }
    if (frames == 0) return 0.0f;
    return (float)frames / clip.fps;
}
