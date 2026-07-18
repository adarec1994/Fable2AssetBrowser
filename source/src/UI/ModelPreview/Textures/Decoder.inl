static thread_local std::string g_last_decode_fail_reason;
static thread_local std::string g_last_decode_info;

const std::string& mp_last_decode_fail_reason() { return g_last_decode_fail_reason; }
const std::string& mp_last_decode_info()        { return g_last_decode_info; }

#define DEC_FAIL(reason) do { g_last_decode_fail_reason = (reason); return false; } while (0)

bool decode_tex_to_rgba(const std::vector<unsigned char>& blob,
                        std::vector<uint8_t>& rgba,
                        int& out_w, int& out_h, bool* out_has_alpha,
                        int mip_index) {
    g_last_decode_fail_reason.clear();
    g_last_decode_info.clear();
    if (out_has_alpha) *out_has_alpha = false;
    TexInfo ti{};
    if (!parse_tex_info(blob, ti)) DEC_FAIL("parse_tex_info_failed");
    if (ti.Mips.empty())            DEC_FAIL("zero_mips");
    {
        std::ostringstream os;
        os << "pf=" << (int)ti.PixelFormat
           << " mips=" << ti.Mips.size()
           << " w=" << (int)ti.TextureWidth
           << " h=" << (int)ti.TextureHeight;
        if (!ti.Mips.empty()) {
            os << " cf0=" << (int)ti.Mips[0].CompFlag
               << " ds0=" << (size_t)ti.Mips[0].DataSize;
        }
        g_last_decode_info = os.str();
    }

    auto mip_wh = [&](size_t i, int& mw, int& mh) {
        const auto& m = ti.Mips[i];
        mw = m.HasWH ? (int)m.MipWidth  : std::max(1, (int)ti.TextureWidth  >> (int)i);
        mh = m.HasWH ? (int)m.MipHeight : std::max(1, (int)ti.TextureHeight >> (int)i);
    };

    auto any_alpha_lt_255 = [&](const std::vector<uint8_t>& buf) -> bool {
        const uint8_t* p = buf.data();
        size_t n = buf.size();
        for (size_t i = 3; i < n; i += 4)
            if (p[i] < 255) return true;
        return false;
    };

    size_t best = 0;
    if (mip_index >= 0 && (size_t)mip_index < ti.Mips.size()) {
        best = (size_t)mip_index;
    } else {
        size_t match_idx = ti.Mips.size();
        for (size_t i = 0; i < ti.Mips.size(); ++i) {
            int w_i = 0, h_i = 0; mip_wh(i, w_i, h_i);
            if ((uint32_t)w_i == ti.TextureWidth &&
                (uint32_t)h_i == ti.TextureHeight) {
                match_idx = i;
                break;
            }
        }
        if (match_idx < ti.Mips.size()) {
            best = match_idx;
        } else {
            int bw = 0, bh = 0; mip_wh(0, bw, bh);
            size_t best_area = (size_t)bw * (size_t)bh;
            for (size_t i = 1; i < ti.Mips.size(); ++i) {
                int wm = 0, hm = 0; mip_wh(i, wm, hm);
                size_t area = (size_t)wm * (size_t)hm;
                if (area > best_area) { best_area = area; best = i; }
            }
        }
    }

    const auto& m = ti.Mips[best];
    int w = 0, h = 0; mip_wh(best, w, h);

    const bool is_zlib_sentinel =
        (m.CompFlag >= 200 && m.CompFlag <= 203);
    if (!is_zlib_sentinel &&
        m.MipDataOffset + m.MipDataSizeParsed > blob.size()) {
        std::ostringstream os;
        os << "mip[" << best << "] data out of bounds (offset=" << m.MipDataOffset
           << " size=" << m.MipDataSizeParsed << " blob=" << blob.size() << ")";
        DEC_FAIL("mip_oob");
    }

    if (m.CompFlag == 200 || m.CompFlag == 201 ||
        m.CompFlag == 202 || m.CompFlag == 203)
    {
        const size_t expected_raw = (size_t)m.Unknown_3;
        if (expected_raw == 0) DEC_FAIL("zlib_no_raw_size");

        std::vector<uint8_t> raw(expected_raw);
        z_stream zs{};
        zs.next_in   = const_cast<Bytef*>(blob.data() + m.MipDataOffset);
        zs.avail_in  = (uInt)m.MipDataSizeParsed;
        zs.next_out  = raw.data();
        zs.avail_out = (uInt)expected_raw;
        if (inflateInit2(&zs, 15) != Z_OK) DEC_FAIL("zlib_init_fail");
        int rc = inflate(&zs, Z_FINISH);
        const size_t produced = expected_raw - zs.avail_out;
        inflateEnd(&zs);
        if (produced != expected_raw &&
            !(rc == Z_OK || rc == Z_STREAM_END || rc == Z_BUF_ERROR)) {
            std::ostringstream os;
            os << "zlib_inflate_fail rc=" << rc
               << " produced=" << produced;
            g_last_decode_info += " " + os.str();
            DEC_FAIL("zlib_inflate_fail");
        }

        out_w = w; out_h = h;

        if (m.CompFlag == 200) {
            std::vector<uint8_t> linear;
            untile_xbox360_imageheat(raw.data(), raw.size(), linear,
                                     w, h, 4, 8);
            swap_bc1_endian(linear.data(), linear.size());
            blit_bc1_to_rgba(linear.data(), w, h, rgba);
            if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
            return true;
        }
        if (m.CompFlag == 201) {
            std::vector<uint8_t> linear;
            untile_xbox360_imageheat(raw.data(), raw.size(), linear,
                                     w, h, 4, 16);
            swap_bc3_endian(linear.data(), linear.size());
            blit_bc3_to_rgba(linear.data(), w, h, rgba);
            if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
            return true;
        }
        if (m.CompFlag == 202) {
            std::vector<uint8_t> linear;
            untile_xbox360_imageheat(raw.data(), raw.size(), linear,
                                     w, h, 4, 16);
            swap_bc5_endian(linear.data(), linear.size());
            blit_bc5_to_rgba(linear.data(), w, h, rgba);
            if (out_has_alpha) *out_has_alpha = false;
            return true;
        }
        const size_t pixels = (size_t)w * (size_t)h;
        if (raw.size() < pixels * 4) DEC_FAIL("zlib_argb8_size_short");
        rgba.assign(pixels * 4, 0);
        const uint32_t W2 = (uint32_t)w;
        const uint32_t H2 = (uint32_t)h;
        const uint32_t padded_W = (W2 + 31u) & ~31u;
        const uint32_t padded_H = (H2 + 31u) & ~31u;
        const uint32_t total    = padded_W * padded_H;
        for (uint32_t off = 0; off < total; ++off) {
            const uint32_t sx = xg_address_2d_tiled_x(off, padded_W, 4);
            const uint32_t sy = xg_address_2d_tiled_y(off, padded_W, 4);
            if (sx >= W2 || sy >= H2) continue;
            const size_t src_byte = (size_t)off * 4;
            if (src_byte + 4 > raw.size()) continue;
            const uint8_t A = raw[src_byte + 0];
            const uint8_t R = raw[src_byte + 1];
            const uint8_t G = raw[src_byte + 2];
            const uint8_t B = raw[src_byte + 3];
            const size_t dst = ((size_t)sy * W2 + sx) * 4;
            rgba[dst + 0] = R;
            rgba[dst + 1] = G;
            rgba[dst + 2] = B;
            rgba[dst + 3] = A;
        }
        if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
        return true;
    }

    if (m.CompFlag == 99 || m.CompFlag == 100) {
        const size_t pixels = (size_t)w * (size_t)h;
        if (m.MipDataSizeParsed < pixels * 4) {
            DEC_FAIL("argb8_size_short");
        }

        const uint8_t* tiled   = blob.data() + m.MipDataOffset;
        const size_t   tiled_n = m.MipDataSizeParsed;
        const uint32_t W       = (uint32_t)w;
        const uint32_t H       = (uint32_t)h;
        const uint32_t padded_W = (W + 31u) & ~31u;
        const uint32_t padded_H = (H + 31u) & ~31u;
        const uint32_t total    = padded_W * padded_H;

        rgba.assign(pixels * 4, 0);

        for (uint32_t off = 0; off < total; ++off) {
            const uint32_t sx = xg_address_2d_tiled_x(off, padded_W, 4);
            const uint32_t sy = xg_address_2d_tiled_y(off, padded_W, 4);
            if (sx >= W || sy >= H) continue;

            const size_t src_byte = (size_t)off * 4;
            if (src_byte + 4 > tiled_n) continue;

            const uint8_t A = tiled[src_byte + 0];
            const uint8_t R = tiled[src_byte + 1];
            const uint8_t G = tiled[src_byte + 2];
            const uint8_t B = tiled[src_byte + 3];
            const size_t  dst = ((size_t)sy * W + sx) * 4;
            rgba[dst + 0] = R;
            rgba[dst + 1] = G;
            rgba[dst + 2] = B;
            rgba[dst + 3] = (m.CompFlag == 100) ? 0xFFu : A;
        }

        out_w = w; out_h = h;
        if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
        return true;
    }

    const size_t bx = (size_t)((w + 3) / 4);
    const size_t by = (size_t)((h + 3) / 4);
    const size_t sz_bc1 = bx * by * 8;
    const size_t sz_bc3 = bx * by * 16;
    const uint8_t* src = blob.data() + m.MipDataOffset;

    if (m.CompFlag == 7) {
        if (ti.PixelFormat == 35) {
            if (m.MipDataSizeParsed < sz_bc1) {
                DEC_FAIL("c7_pf35_size_short");
            }
            std::vector<uint8_t> linear(src, src + sz_bc1);
            swap_bc1_endian(linear.data(), linear.size());
            blit_bc1_to_rgba(linear.data(), w, h, rgba);
            out_w = w; out_h = h;
            if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
            return true;
        }
        if (ti.PixelFormat == 39) {
            if (m.MipDataSizeParsed < sz_bc3) {
                DEC_FAIL("c7_pf39_size_short");
            }
            std::vector<uint8_t> linear(src, src + sz_bc3);
            swap_bc3_endian(linear.data(), linear.size());
            blit_bc3_to_rgba(linear.data(), w, h, rgba);
            out_w = w; out_h = h;
            if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
            return true;
        }
        if (ti.PixelFormat == 40) {
            if (m.MipDataSizeParsed < sz_bc3) {
                DEC_FAIL("c7_pf40_size_short");
            }
            std::vector<uint8_t> linear(src, src + sz_bc3);
            swap_bc5_endian(linear.data(), linear.size());
            blit_bc5_to_rgba(linear.data(), w, h, rgba);
            out_w = w; out_h = h;
            if (out_has_alpha) *out_has_alpha = false;
            return true;
        }

        size_t sz_raw = (size_t)w * (size_t)h * 4;
        if (m.MipDataSizeParsed < sz_raw) {
            std::ostringstream os;
            os << "unknown raw format and data too small for RGBA ("
               << m.MipDataSizeParsed << " < " << sz_raw
               << ", PixelFormat=" << ti.PixelFormat << ")";
            DEC_FAIL("c7_raw_size_short");
        }
        rgba.assign(sz_raw, 0xFF);
        if (ti.PixelFormat == 2 || ti.PixelFormat == 4) {
            for (size_t i = 0, n = (size_t)w * (size_t)h; i < n; ++i) {
                rgba[i * 4 + 0] = src[i * 4 + 1];
                rgba[i * 4 + 1] = src[i * 4 + 2];
                rgba[i * 4 + 2] = src[i * 4 + 3];
                rgba[i * 4 + 3] = (ti.PixelFormat == 4) ? 0xFFu : src[i * 4 + 0];
            }
        } else {
            memcpy(rgba.data(), src, sz_raw);
        }
        out_w = w; out_h = h;
        if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
        return true;
    }

    if (m.CompFlag == 3 && (ti.PixelFormat == 40 || ti.PixelFormat == 4)) {
        const size_t x_body_start = m.DefOffset + 48;
        const size_t x_body_size  = m.DataSize;
        const bool   has_y_sub    = (m.Unknown_3 == 3) && (m.Unknown_5 > 0) &&
                                    (m.Unknown_4 == 48 + m.DataSize);
        const size_t y_body_start = m.DefOffset + (size_t)m.Unknown_4;
        const size_t y_body_size  = (size_t)m.Unknown_5;

        const bool ranges_ok =
            (x_body_start + x_body_size <= blob.size()) &&
            (!has_y_sub || (y_body_start + y_body_size <= blob.size()));

        if (ranges_ok) {
            std::vector<uint8_t> bc4_x, bc4_y;
            std::string err_x, err_y;
            const bool ok_x = lh_decode_variant_2_3_4(
                blob.data() + x_body_start, x_body_size, 2, w, h, bc4_x, &err_x);
            const bool ok_y = has_y_sub
                ? lh_decode_variant_2_3_4(
                      blob.data() + y_body_start, y_body_size, 2, w, h, bc4_y, &err_y)
                : false;

            if (ok_x) {
                const size_t n_blocks =
                    ((size_t)w / 4u + ((size_t)w % 4u != 0u)) *
                    ((size_t)h / 4u + ((size_t)h % 4u != 0u));
                std::vector<uint8_t> bc5_blocks(n_blocks * 16);
                for (size_t i = 0; i < n_blocks; ++i) {

                    memcpy(bc5_blocks.data() + i * 16, bc4_x.data() + i * 8, 8);
                    if (ok_y) {

                        memcpy(bc5_blocks.data() + i * 16 + 8, bc4_y.data() + i * 8, 8);
                    } else {

                        bc5_blocks[i * 16 + 8] = 0x80;
                        bc5_blocks[i * 16 + 9] = 0x80;
                        for (int k = 10; k < 16; ++k) bc5_blocks[i * 16 + k] = 0;
                    }
                }
                blit_bc5_to_rgba(bc5_blocks.data(), w, h, rgba);
                out_w = w; out_h = h;
                if (out_has_alpha) *out_has_alpha = false;
                return true;
            } else {
                std::ostringstream os;
                os << "variant_2_3_4 BC5 X-channel (" << w << "x" << h
                   << ") failed: " << err_x << " - falling back to comp=7";

            }
        }
    }

    {

        const bool pf_is_bc1_family =
            (ti.PixelFormat == 35) || (ti.PixelFormat == 0) || (ti.PixelFormat == 12);
        const bool pf_is_bc3_family =
            (ti.PixelFormat == 39) || (ti.PixelFormat == 1) ||
            (ti.PixelFormat == 2)  || (ti.PixelFormat == 3);
        if (!pf_is_bc1_family && !pf_is_bc3_family) {

            int fallback_idx = -1;
            int fallback_area = 0;
            for (size_t i = 0; i < ti.Mips.size(); ++i) {
                if (ti.Mips[i].CompFlag != 7) continue;
                int fw = 0, fh = 0; mip_wh(i, fw, fh);
                int area = fw * fh;
                if (area > fallback_area) { fallback_area = area; fallback_idx = (int)i; }
            }
            if (fallback_idx < 0) {
                std::ostringstream os;
                os << "PixelFormat " << ti.PixelFormat
                   << " is compressed and no comp=7 fallback exists; "
                      "Lionhead codec port currently handles BC1 (35) only";
                DEC_FAIL("fallback_unhandled_pf");
            }
            const auto& fm = ti.Mips[fallback_idx];
            int fw = 0, fh = 0; mip_wh(fallback_idx, fw, fh);
            const size_t fbx = (size_t)((fw + 3) / 4);
            const size_t fby = (size_t)((fh + 3) / 4);
            if (fm.MipDataOffset + fm.MipDataSizeParsed > blob.size()) {
                DEC_FAIL("fallback_mip_oob");
            }
            const uint8_t* fsrc = blob.data() + fm.MipDataOffset;
            if (ti.PixelFormat == 39 || ti.PixelFormat == 1 ||
                ti.PixelFormat == 2  || ti.PixelFormat == 3) {
                if (fm.MipDataSizeParsed < fbx * fby * 16) {
                    DEC_FAIL("fallback_bc3_size_short");
                }
                std::vector<uint8_t> linear;
                untile_xbox360_bc(fsrc, fm.MipDataSizeParsed, linear, fw, fh, 16);
                swap_bc3_endian(linear.data(), linear.size());
                blit_bc3_to_rgba(linear.data(), fw, fh, rgba);
                out_w = fw; out_h = fh;
                if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
                return true;
            }
            if (ti.PixelFormat == 40) {

                if (fm.MipDataSizeParsed < fbx * fby * 16) {
                    DEC_FAIL("fallback_bc5_size_short");
                }
                std::vector<uint8_t> linear;
                untile_xbox360_bc(fsrc, fm.MipDataSizeParsed, linear, fw, fh, 16);
                swap_bc5_endian(linear.data(), linear.size());
                blit_bc5_to_rgba(linear.data(), fw, fh, rgba);
                out_w = fw; out_h = fh;
                if (out_has_alpha) *out_has_alpha = false;
                return true;
            }
            DEC_FAIL("fallback_no_match");
        }

        const size_t body_start = m.DefOffset + 48;
        const size_t body_size  = m.DataSize;
        if (body_start + body_size > blob.size()) {
            std::ostringstream os;
            os << "compressed mip body OOB (start=" << body_start
               << " size=" << body_size << " blob=" << blob.size() << ")";
            DEC_FAIL("comp_body_oob");
        }
        const uint8_t* body_ptr = blob.data() + body_start;

        std::vector<uint8_t> bc1;
        int dec_w = 0, dec_h = 0;
        std::string err;

        const bool comp11 = (m.CompFlag == 11);
        bool ok = lh_decode_compressed_mip(body_ptr, body_size,
                                           dec_w, dec_h, bc1, &err, comp11);
        if (!ok) {
            g_last_decode_info += " lh_err=\"" + err + "\"";
            DEC_FAIL("lh_decode_failed");
        }

        if (dec_w != w || dec_h != h) {
            std::ostringstream os;
            os << "WARNING: codec reported " << dec_w << "x" << dec_h
               << " but TexInfo says " << w << "x" << h << "; trusting codec";
            w = dec_w; h = dec_h;
        }
        blit_bc1_to_rgba(bc1.data(), w, h, rgba);
        out_w = w; out_h = h;

        if (pf_is_bc3_family && m.Unknown_5 > 0) {
            const size_t a_body_start = m.DefOffset + (size_t)m.Unknown_4;
            const size_t a_body_size  = (size_t)m.Unknown_5;
            const uint32_t a_cf = m.Unknown_3;

            if (a_body_start + a_body_size <= blob.size()) {
                std::vector<uint8_t> alpha_blocks;
                int adec_w = 0, adec_h = 0;
                std::string aerr;
                bool aok = false;

                if (a_cf == 1 || a_cf == 11) {

                    aok = lh_decode_compressed_mip(
                        blob.data() + a_body_start, a_body_size,
                        adec_w, adec_h, alpha_blocks, &aerr,
                        (a_cf == 11));
                } else if (a_cf == 3 || a_cf == 2 || a_cf == 4) {

                    aok = lh_decode_variant_2_3_4(
                        blob.data() + a_body_start, a_body_size,
                        2, w, h, alpha_blocks, &aerr);
                    adec_w = w; adec_h = h;
                } else if (a_cf == 7) {

                    const size_t expected = (size_t)((w + 3) / 4)
                                          * (size_t)((h + 3) / 4) * 8;
                    if (a_body_size >= expected) {
                        alpha_blocks.assign(blob.data() + a_body_start,
                                            blob.data() + a_body_start + expected);

                        for (size_t i = 0; i + 8 <= alpha_blocks.size(); i += 8) {
                            uint8_t* blk = alpha_blocks.data() + i;
                            uint64_t bits = 0;
                            for (int j = 0; j < 6; j++)
                                bits |= ((uint64_t)blk[2+j]) << (j * 8);
                            uint64_t sw = 0;
                            for (int j = 0; j < 6; j++)
                                sw |= ((bits >> (j * 8)) & 0xFF) << ((5 - j) * 8);
                            for (int j = 0; j < 6; j++)
                                blk[2 + j] = (uint8_t)((sw >> (j * 8)) & 0xFF);
                        }
                        adec_w = w; adec_h = h;
                        aok = true;
                    } else {
                        aerr = "alpha sub-block too small for raw BC4";
                    }
                } else {
                    std::ostringstream os;
                    os << "unhandled BC3 alpha CompFlag=" << a_cf;
                    aerr = os.str();
                }

                if (aok && adec_w == w && adec_h == h) {
                    const size_t bx_n = (size_t)((w + 3) / 4);
                    const size_t by_n = (size_t)((h + 3) / 4);
                    if (alpha_blocks.size() >= bx_n * by_n * 8) {
                        for (size_t byy = 0; byy < by_n; ++byy) {
                            for (size_t bxx = 0; bxx < bx_n; ++bxx) {
                                uint8_t avals[16];
                                decode_bc4_block(alpha_blocks.data() + (byy * bx_n + bxx) * 8,
                                                 avals);
                                for (int py = 0; py < 4; ++py) {
                                    int yy = (int)byy * 4 + py;
                                    if (yy >= h) break;
                                    for (int px = 0; px < 4; ++px) {
                                        int xx = (int)bxx * 4 + px;
                                        if (xx >= w) break;
                                        rgba[(yy * w + xx) * 4 + 3] = avals[py * 4 + px];
                                    }
                                }
                            }
                        }
                    }
                } else if (!aok) {

                }
            }
        }

        if (out_has_alpha) *out_has_alpha = any_alpha_lt_255(rgba);
        return true;
    }
}
