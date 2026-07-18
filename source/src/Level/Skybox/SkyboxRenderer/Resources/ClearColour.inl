void GetClearColour(const ModelPreview& preview,
                    const FrameState& frame,
                    float out_rgba[4])
{
    out_rgba[0] = 0.22f;
    out_rgba[1] = 0.22f;
    out_rgba[2] = 0.22f;
    out_rgba[3] = 1.0f;
#if FABLE_ENABLE_RECONSTRUCTED_SKY_PREVIEW
    if (preview.has_sky_theme && preview.show_sky) {
        out_rgba[0] = std::clamp(frame.sky_bottom[0], 0.0f, 1.0f);
        out_rgba[1] = std::clamp(frame.sky_bottom[1], 0.0f, 1.0f);
        out_rgba[2] = std::clamp(frame.sky_bottom[2], 0.0f, 1.0f);
    }
#else
    (void)preview;
    (void)frame;
#endif
}
