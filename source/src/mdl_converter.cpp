// mdl_converter.cpp
#include "mdl_converter.h"
#include "ModelParser.h"
#include "TexParser.h"
#include "Files.h"
#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <cmath>

namespace {

static inline std::string json_escape(const std::string& s){
    std::string o; o.reserve(s.size()+8);
    for (unsigned char c: s){
        switch(c){
            case '\"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\b': o += "\\b"; break;
            case '\f': o += "\\f"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20){ char buf[7]; std::snprintf(buf,sizeof(buf),"\\u%04x",(int)c); o += buf; }
                else o += (char)c;
        }
    }
    return o;
}

struct Matrix4 {
    float m[16];

    Matrix4() {
        for(int i=0;i<16;++i) m[i]=0.0f;
        m[0]=m[5]=m[10]=m[15]=1.0f;
    }

    static Matrix4 fromTRS(float qx, float qy, float qz, float qw,
                           float tx, float ty, float tz,
                           float sx, float sy, float sz) {
        Matrix4 mat;
        float x2 = qx + qx, y2 = qy + qy, z2 = qz + qz;
        float xx = qx * x2, yy = qy * y2, zz = qz * z2;
        float xy = qx * y2, xz = qx * z2, yz = qy * z2;
        float wx = qw * x2, wy = qw * y2, wz = qw * z2;

        mat.m[0] = (1.0f - (yy + zz)) * sx;
        mat.m[1] = (xy + wz) * sx;
        mat.m[2] = (xz - wy) * sx;
        mat.m[3] = 0.0f;

        mat.m[4] = (xy - wz) * sy;
        mat.m[5] = (1.0f - (xx + zz)) * sy;
        mat.m[6] = (yz + wx) * sy;
        mat.m[7] = 0.0f;

        mat.m[8] = (xz + wy) * sz;
        mat.m[9] = (yz - wx) * sz;
        mat.m[10] = (1.0f - (xx + yy)) * sz;
        mat.m[11] = 0.0f;

        mat.m[12] = tx;
        mat.m[13] = ty;
        mat.m[14] = tz;
        mat.m[15] = 1.0f;

        return mat;
    }

    Matrix4 operator*(const Matrix4& other) const {
        Matrix4 result;
        for(int c=0; c<4; ++c) {
            for(int r=0; r<4; ++r) {
                result.m[c*4+r] =
                    m[0*4+r] * other.m[c*4+0] +
                    m[1*4+r] * other.m[c*4+1] +
                    m[2*4+r] * other.m[c*4+2] +
                    m[3*4+r] * other.m[c*4+3];
            }
        }
        return result;
    }

    Matrix4 inverse() const {
        float r0[3] = {m[0], m[1], m[2]};
        float r1[3] = {m[4], m[5], m[6]};
        float r2[3] = {m[8], m[9], m[10]};
        float t[3] = {m[12], m[13], m[14]};

        float a=r0[0], b=r0[1], c=r0[2];
        float d=r1[0], e=r1[1], f=r1[2];
        float g=r2[0], h=r2[1], i=r2[2];

        float A = e*i - f*h;
        float B = -(d*i - f*g);
        float C = d*h - e*g;
        float det = a*A + b*B + c*C;

        if(std::abs(det) < 1e-8f) {
            Matrix4 result;
            result.m[12] = -t[0];
            result.m[13] = -t[1];
            result.m[14] = -t[2];
            return result;
        }

        float invdet = 1.0f / det;
        Matrix4 result;

        result.m[0] = A * invdet;
        result.m[1] = (c*h - b*i) * invdet;
        result.m[2] = (b*f - c*e) * invdet;
        result.m[3] = 0.0f;

        result.m[4] = B * invdet;
        result.m[5] = (a*i - c*g) * invdet;
        result.m[6] = (c*d - a*f) * invdet;
        result.m[7] = 0.0f;

        result.m[8] = C * invdet;
        result.m[9] = (b*g - a*h) * invdet;
        result.m[10] = (a*e - b*d) * invdet;
        result.m[11] = 0.0f;

        result.m[12] = -(result.m[0]*t[0] + result.m[4]*t[1] + result.m[8]*t[2]);
        result.m[13] = -(result.m[1]*t[0] + result.m[5]*t[1] + result.m[9]*t[2]);
        result.m[14] = -(result.m[2]*t[0] + result.m[6]*t[1] + result.m[10]*t[2]);
        result.m[15] = 1.0f;

        return result;
    }
};

static inline uint8_t ex5(uint16_t v){ return (uint8_t)((v<<3)|(v>>2)); }
static inline uint8_t ex6(uint16_t v){ return (uint8_t)((v<<2)|(v>>4)); }

static void decode_bc1_block(const uint8_t* b, uint32_t* outRGBA) {
    uint16_t c0 = (uint16_t)(b[0] | (b[1]<<8));
    uint16_t c1 = (uint16_t)(b[2] | (b[3]<<8));
    uint8_t r0=ex5((c0>>11)&31), g0=ex6((c0>>5)&63), b0=ex5(c0&31);
    uint8_t r1=ex5((c1>>11)&31), g1=ex6((c1>>5)&63), b1=ex5(c1&31);
    uint32_t cols[4];
    cols[0] = (0xFFu<<24) | (b0<<16) | (g0<<8) | r0;
    cols[1] = (0xFFu<<24) | (b1<<16) | (g1<<8) | r1;
    if(c0 > c1){
        cols[2] = (0xFFu<<24) | (((2*b0+b1)/3)<<16) | (((2*g0+g1)/3)<<8) | ((2*r0+r1)/3);
        cols[3] = (0xFFu<<24) | (((b0+2*b1)/3)<<16) | (((g0+2*g1)/3)<<8) | ((r0+2*r1)/3);
    }else{
        cols[2] = (0xFFu<<24) | (((b0+b1)>>1)<<16) | (((g0+g1)>>1)<<8) | ((r0+r1)>>1);
        cols[3] = 0x00000000u;
    }
    const uint32_t idx = b[4] | (b[5]<<8) | (b[6]<<16) | (b[7]<<24);
    for(int py=0; py<4; ++py){
        for(int px=0; px<4; ++px){
            int s = (idx >> (2*(py*4+px))) & 3;
            outRGBA[py*4+px] = cols[s];
        }
    }
}

static void decode_bc3_block(const uint8_t* b, uint32_t* outRGBA) {
    uint8_t a0 = b[0], a1 = b[1];
    uint64_t abits = 0;
    for(int i=0; i<6; ++i) abits |= (uint64_t)b[2+i] << (8*i);

    uint8_t atab[8];
    atab[0] = a0; atab[1] = a1;
    if(a0 > a1) {
        for(int i=1; i<=6; i++) atab[i+1] = (uint8_t)(((7-i)*a0 + i*a1)/7);
    } else {
        for(int i=1; i<=4; i++) atab[i+1] = (uint8_t)(((5-i)*a0 + i*a1)/5);
        atab[6] = 0; atab[7] = 255;
    }

    uint32_t color[16];
    decode_bc1_block(b+8, color);

    for(int i=0; i<16; ++i) {
        uint8_t ai = (uint8_t)((abits >> (3*i)) & 7);
        color[i] = (color[i] & 0x00FFFFFFu) | (((uint32_t)atab[ai]) << 24);
    }

    for(int i=0; i<16; ++i) outRGBA[i] = color[i];
}

static void decode_bc5_block(const uint8_t* b, uint32_t* outRGBA) {
    uint8_t r0 = b[0], r1 = b[1];
    uint64_t rbits = 0;
    for(int i=0; i<6; ++i) rbits |= (uint64_t)b[2+i] << (8*i);

    uint8_t g0 = b[8], g1 = b[9];
    uint64_t gbits = 0;
    for(int i=0; i<6; ++i) gbits |= (uint64_t)b[10+i] << (8*i);

    uint8_t rtab[8], gtab[8];
    rtab[0] = r0; rtab[1] = r1;
    if(r0 > r1) {
        for(int i=1; i<=6; i++) rtab[i+1] = (uint8_t)(((7-i)*r0 + i*r1)/7);
    } else {
        for(int i=1; i<=4; i++) rtab[i+1] = (uint8_t)(((5-i)*r0 + i*r1)/5);
        rtab[6] = 0; rtab[7] = 255;
    }

    gtab[0] = g0; gtab[1] = g1;
    if(g0 > g1) {
        for(int i=1; i<=6; i++) gtab[i+1] = (uint8_t)(((7-i)*g0 + i*g1)/7);
    } else {
        for(int i=1; i<=4; i++) gtab[i+1] = (uint8_t)(((5-i)*g0 + i*g1)/5);
        gtab[6] = 0; gtab[7] = 255;
    }

    for(int i=0; i<16; ++i) {
        uint8_t ri = (uint8_t)((rbits >> (3*i)) & 7);
        uint8_t gi = (uint8_t)((gbits >> (3*i)) & 7);
        uint8_t r = rtab[ri];
        uint8_t g = gtab[gi];

        float nx = (r / 255.0f) * 2.0f - 1.0f;
        float ny = (g / 255.0f) * 2.0f - 1.0f;
        float nz = sqrtf(std::max(0.0f, 1.0f - nx*nx - ny*ny));

        uint8_t br = (uint8_t)((nx * 0.5f + 0.5f) * 255.0f);
        uint8_t bg = (uint8_t)((ny * 0.5f + 0.5f) * 255.0f);
        uint8_t bb = (uint8_t)((nz * 0.5f + 0.5f) * 255.0f);

        outRGBA[i] = (0xFFu << 24) | (bb << 16) | (bg << 8) | br;
    }
}

static void swap_bc1_endian(uint8_t* data, size_t size) {
    for(size_t i = 0; i + 8 <= size; i += 8) {
        uint16_t c0 = (data[i+0] << 8) | data[i+1];
        uint16_t c1 = (data[i+2] << 8) | data[i+3];
        uint32_t idx = (data[i+4] << 24) | (data[i+5] << 16) | (data[i+6] << 8) | data[i+7];
        data[i+0] = c0 & 0xFF;
        data[i+1] = (c0 >> 8) & 0xFF;
        data[i+2] = c1 & 0xFF;
        data[i+3] = (c1 >> 8) & 0xFF;
        data[i+4] = idx & 0xFF;
        data[i+5] = (idx >> 8) & 0xFF;
        data[i+6] = (idx >> 16) & 0xFF;
        data[i+7] = (idx >> 24) & 0xFF;
    }
}

static void swap_bc3_endian(uint8_t* data, size_t size) {
    for(size_t i = 0; i + 16 <= size; i += 16) {
        uint64_t alpha_bits = 0;
        for(int j = 0; j < 6; j++) {
            alpha_bits |= ((uint64_t)data[i+2+j]) << (j*8);
        }

        uint64_t alpha_swapped = 0;
        for(int j = 0; j < 6; j++) {
            alpha_swapped |= ((alpha_bits >> (j*8)) & 0xFF) << ((5-j)*8);
        }

        for(int j = 0; j < 6; j++) {
            data[i+2+j] = (alpha_swapped >> (j*8)) & 0xFF;
        }

        swap_bc1_endian(data + i + 8, 8);
    }
}

static void swap_bc5_endian(uint8_t* data, size_t size) {
    for(size_t i = 0; i + 16 <= size; i += 16) {
        uint64_t r_bits = 0;
        for(int j = 0; j < 6; j++) {
            r_bits |= ((uint64_t)data[i+2+j]) << (j*8);
        }

        uint64_t r_swapped = 0;
        for(int j = 0; j < 6; j++) {
            r_swapped |= ((r_bits >> (j*8)) & 0xFF) << ((5-j)*8);
        }

        for(int j = 0; j < 6; j++) {
            data[i+2+j] = (r_swapped >> (j*8)) & 0xFF;
        }

        uint64_t g_bits = 0;
        for(int j = 0; j < 6; j++) {
            g_bits |= ((uint64_t)data[i+10+j]) << (j*8);
        }

        uint64_t g_swapped = 0;
        for(int j = 0; j < 6; j++) {
            g_swapped |= ((g_bits >> (j*8)) & 0xFF) << ((5-j)*8);
        }

        for(int j = 0; j < 6; j++) {
            data[i+10+j] = (g_swapped >> (j*8)) & 0xFF;
        }
    }
}

static bool decode_texture_to_png(const std::vector<unsigned char>& tex_buf, std::vector<uint8_t>& png_out) {
    TexInfo tex_info;
    if (!parse_tex_info(tex_buf, tex_info) || tex_info.Mips.empty()) return false;

    size_t best = 0;
    size_t best_area = 0;
    for (size_t i = 0; i < tex_info.Mips.size(); ++i) {
        if (tex_info.Mips[i].CompFlag != 7) continue;
        int w = tex_info.Mips[i].HasWH ? (int)tex_info.Mips[i].MipWidth : std::max(1, (int)tex_info.TextureWidth >> (int)i);
        int h = tex_info.Mips[i].HasWH ? (int)tex_info.Mips[i].MipHeight : std::max(1, (int)tex_info.TextureHeight >> (int)i);
        size_t area = (size_t)w * (size_t)h;
        if (area > best_area) {
            best_area = area;
            best = i;
        }
    }

    const auto& mip = tex_info.Mips[best];
    int w = mip.HasWH ? (int)mip.MipWidth : std::max(1, (int)tex_info.TextureWidth >> (int)best);
    int h = mip.HasWH ? (int)mip.MipHeight : std::max(1, (int)tex_info.TextureHeight >> (int)best);

    if (mip.MipDataOffset + mip.MipDataSizeParsed > tex_buf.size()) return false;

    size_t bx = (size_t)((w+3)/4), by = (size_t)((h+3)/4);
    std::vector<uint8_t> rgba((size_t)w*(size_t)h*4, 0xFF);
    const uint8_t* src = tex_buf.data() + mip.MipDataOffset;

    if (tex_info.PixelFormat == 35) {
        size_t sz_bc1 = bx*by*8;
        if (mip.MipDataSizeParsed < sz_bc1) return false;

        std::vector<uint8_t> swapped(src, src + sz_bc1);
        swap_bc1_endian(swapped.data(), swapped.size());

        size_t off=0;
        for(size_t byy=0; byy<by; ++byy){
            for(size_t bxx=0; bxx<bx; ++bxx){
                uint32_t block[16];
                decode_bc1_block(swapped.data()+off, block); off += 8;
                for(int py=0; py<4; ++py){
                    int yy = (int)byy*4 + py; if(yy>=h) break;
                    for(int px=0; px<4; ++px){
                        int xx=(int)bxx*4 + px; if(xx>=w) break;
                        ((uint32_t*)rgba.data())[yy*w+xx] = block[py*4+px];
                    }
                }
            }
        }
    } else if (tex_info.PixelFormat == 39) {
        size_t sz_bc3 = bx*by*16;
        if (mip.MipDataSizeParsed < sz_bc3) return false;

        std::vector<uint8_t> swapped(src, src + sz_bc3);
        swap_bc3_endian(swapped.data(), swapped.size());

        size_t off=0;
        for(size_t byy=0; byy<by; ++byy){
            for(size_t bxx=0; bxx<bx; ++bxx){
                uint32_t block[16];
                decode_bc3_block(swapped.data()+off, block); off += 16;
                for(int py=0; py<4; ++py){
                    int yy = (int)byy*4 + py; if(yy>=h) break;
                    for(int px=0; px<4; ++px){
                        int xx=(int)bxx*4 + px; if(xx>=w) break;
                        ((uint32_t*)rgba.data())[yy*w+xx] = block[py*4+px];
                    }
                }
            }
        }
    } else if (tex_info.PixelFormat == 40) {
        size_t sz_bc5 = bx*by*16;
        if (mip.MipDataSizeParsed < sz_bc5) return false;

        std::vector<uint8_t> swapped(src, src + sz_bc5);
        swap_bc5_endian(swapped.data(), swapped.size());

        size_t off=0;
        for(size_t byy=0; byy<by; ++byy){
            for(size_t bxx=0; bxx<bx; ++bxx){
                uint32_t block[16];
                decode_bc5_block(swapped.data()+off, block); off += 16;
                for(int py=0; py<4; ++py){
                    int yy = (int)byy*4 + py; if(yy>=h) break;
                    for(int px=0; px<4; ++px){
                        int xx=(int)bxx*4 + px; if(xx>=w) break;
                        ((uint32_t*)rgba.data())[yy*w+xx] = block[py*4+px];
                    }
                }
            }
        }
    } else {
        return false;
    }

    png_out.clear();
    png_out.push_back(0x89); png_out.push_back(0x50); png_out.push_back(0x4E); png_out.push_back(0x47);
    png_out.push_back(0x0D); png_out.push_back(0x0A); png_out.push_back(0x1A); png_out.push_back(0x0A);

    auto write_chunk = [&](const char* type, const std::vector<uint8_t>& data) {
        uint32_t len = (uint32_t)data.size();
        png_out.push_back((len>>24)&0xFF);
        png_out.push_back((len>>16)&0xFF);
        png_out.push_back((len>>8)&0xFF);
        png_out.push_back(len&0xFF);

        size_t crc_start = png_out.size();
        png_out.push_back(type[0]); png_out.push_back(type[1]);
        png_out.push_back(type[2]); png_out.push_back(type[3]);
        png_out.insert(png_out.end(), data.begin(), data.end());

        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = crc_start; i < png_out.size(); ++i) {
            uint8_t byte = png_out[i];
            crc ^= byte;
            for (int j = 0; j < 8; ++j) {
                if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
                else crc >>= 1;
            }
        }
        crc ^= 0xFFFFFFFF;
        png_out.push_back((crc>>24)&0xFF);
        png_out.push_back((crc>>16)&0xFF);
        png_out.push_back((crc>>8)&0xFF);
        png_out.push_back(crc&0xFF);
    };

    std::vector<uint8_t> ihdr(13);
    ihdr[0] = (w>>24)&0xFF; ihdr[1] = (w>>16)&0xFF; ihdr[2] = (w>>8)&0xFF; ihdr[3] = w&0xFF;
    ihdr[4] = (h>>24)&0xFF; ihdr[5] = (h>>16)&0xFF; ihdr[6] = (h>>8)&0xFF; ihdr[7] = h&0xFF;
    ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    write_chunk("IHDR", ihdr);

    std::vector<uint8_t> idat_raw;
    for (int y = 0; y < h; ++y) {
        idat_raw.push_back(0);
        for (int x = 0; x < w; ++x) {
            uint32_t px = ((uint32_t*)rgba.data())[y*w+x];
            idat_raw.push_back((px>>0)&0xFF);
            idat_raw.push_back((px>>8)&0xFF);
            idat_raw.push_back((px>>16)&0xFF);
            idat_raw.push_back((px>>24)&0xFF);
        }
    }

    std::vector<uint8_t> idat_compressed;
    idat_compressed.push_back(0x78); idat_compressed.push_back(0x01);

    size_t block_size = 65535;
    for (size_t pos = 0; pos < idat_raw.size(); pos += block_size) {
        size_t chunk = std::min(block_size, idat_raw.size() - pos);
        bool last = (pos + chunk >= idat_raw.size());

        idat_compressed.push_back(last ? 0x01 : 0x00);
        idat_compressed.push_back(chunk & 0xFF);
        idat_compressed.push_back((chunk >> 8) & 0xFF);
        idat_compressed.push_back((~chunk) & 0xFF);
        idat_compressed.push_back(((~chunk) >> 8) & 0xFF);

        idat_compressed.insert(idat_compressed.end(), idat_raw.begin() + pos, idat_raw.begin() + pos + chunk);
    }

    uint32_t s1 = 1, s2 = 0;
    for (uint8_t byte : idat_raw) {
        s1 = (s1 + byte) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    uint32_t adler = (s2 << 16) | s1;

    idat_compressed.push_back((adler>>24)&0xFF);
    idat_compressed.push_back((adler>>16)&0xFF);
    idat_compressed.push_back((adler>>8)&0xFF);
    idat_compressed.push_back(adler&0xFF);

    write_chunk("IDAT", idat_compressed);
    write_chunk("IEND", {});

    return true;
}
}

bool mdl_to_glb_full(const std::vector<unsigned char>& mdl_data,
                     const std::string& glb_path,
                     const std::string& mdl_source_path,
                     std::string& err_msg)
{
    err_msg.clear();

    MDLInfo info;
    if (!parse_mdl_info(mdl_data, info, mdl_source_path)) {
        err_msg = "Failed to parse MDL info";
        return false;
    }

    std::vector<MDLMeshGeom> geoms;
    if (!parse_mdl_geometry(mdl_data, info, geoms)) {
        err_msg = "Failed to parse MDL geometry";
        return false;
    }

    if (geoms.empty()) {
        err_msg = "No geometry found";
        return false;
    }

    std::string model_name = std::filesystem::path(glb_path).stem().string();

    std::vector<uint8_t> bin_data;
    std::ostringstream json;
    std::ostringstream bufferViews;
    std::ostringstream accessors;
    std::ostringstream images;
    std::ostringstream textures;
    std::ostringstream materials;
    std::ostringstream meshes;
    std::ostringstream bone_nodes;

    int bv_count = 0;
    int acc_count = 0;
    int img_count = 0;
    int tex_count = 0;
    int mat_count = 0;
    int mesh_count = 0;

    auto add_data = [&](const void* data, size_t size) -> size_t {
        size_t offset = bin_data.size();
        const uint8_t* p = (const uint8_t*)data;
        bin_data.insert(bin_data.end(), p, p + size);
        while (bin_data.size() & 3) bin_data.push_back(0);
        return offset;
    };

    struct ValidBone {
        int original_index;
        int node_index;
        std::string name;
        int parent_original;
        std::vector<float> transform;
    };

    std::vector<ValidBone> valid_bones;
    std::vector<int> original_to_node(info.BoneCount, -1);
    int bone_node_count = 0;

    for (size_t i = 0; i < info.Bones.size(); ++i) {
        const auto& bone = info.Bones[i];
        if (bone.Name.find("Rig_Asset") != std::string::npos) continue;

        ValidBone vb;
        vb.original_index = (int)i;
        vb.node_index = bone_node_count++;
        vb.name = bone.Name;
        vb.parent_original = bone.ParentID;

        if (i < info.BoneTransforms.size() && info.BoneTransforms[i].size() >= 10) {
            vb.transform = info.BoneTransforms[i];
        }

        original_to_node[i] = vb.node_index;
        valid_bones.push_back(vb);
    }

    std::vector<std::vector<int>> bone_children(valid_bones.size());
    std::vector<int> scene_root_bone_nodes;
    std::vector<int> node_parents(valid_bones.size(), -1);

    for (size_t i = 0; i < valid_bones.size(); ++i) {
        int parent_orig = valid_bones[i].parent_original;
        if (parent_orig >= 0 && parent_orig < (int)original_to_node.size()) {
            int parent_node = original_to_node[parent_orig];
            if (parent_node >= 0 && parent_node != valid_bones[i].node_index) {
                bone_children[parent_node].push_back(valid_bones[i].node_index);
                node_parents[valid_bones[i].node_index] = parent_node;
                continue;
            }
        }
        scene_root_bone_nodes.push_back(valid_bones[i].node_index);
    }

    std::vector<Matrix4> local_transforms(valid_bones.size());
    for (size_t i = 0; i < valid_bones.size(); ++i) {
        if (!valid_bones[i].transform.empty() && valid_bones[i].transform.size() >= 10) {
            const auto& tf = valid_bones[i].transform;
            local_transforms[i] = Matrix4::fromTRS(
                tf[0], tf[1], tf[2], tf[3],
                tf[4], tf[5], tf[6],
                tf[7], tf[8], tf[9]
            );
        }
    }

    std::vector<Matrix4> global_transforms(valid_bones.size());
    for (size_t i = 0; i < valid_bones.size(); ++i) {
        std::vector<int> chain;
        int cur = i;
        while (cur >= 0) {
            chain.push_back(cur);
            cur = node_parents[cur];
        }

        Matrix4 m;
        for (int j = (int)chain.size() - 1; j >= 0; --j) {
            m = m * local_transforms[chain[j]];
        }
        global_transforms[i] = m;
    }

    for (size_t i = 0; i < valid_bones.size(); ++i) {
        if (i > 0) bone_nodes << ",";
        bone_nodes << "{\"name\":\"" << json_escape(valid_bones[i].name) << "\"";

        if (!bone_children[i].empty()) {
            bone_nodes << ",\"children\":[";
            for (size_t j = 0; j < bone_children[i].size(); ++j) {
                if (j > 0) bone_nodes << ",";
                bone_nodes << bone_children[i][j];
            }
            bone_nodes << "]";
        }

        if (!valid_bones[i].transform.empty() && valid_bones[i].transform.size() >= 10) {
            const auto& tf = valid_bones[i].transform;
            bone_nodes << ",\"rotation\":[" << tf[0] << "," << tf[1] << "," << tf[2] << "," << tf[3] << "]";
            bone_nodes << ",\"translation\":[" << tf[4] << "," << tf[5] << "," << tf[6] << "]";
            bone_nodes << ",\"scale\":[" << tf[7] << "," << tf[8] << "," << tf[9] << "]";
        }

        bone_nodes << "}";
    }

    std::vector<int> joint_indices;
    for (const auto& vb : valid_bones) {
        joint_indices.push_back(vb.node_index);
    }

    int skin_idx = -1;
    int ibm_acc = -1;
    if (!valid_bones.empty()) {
        std::vector<float> ibm_data;
        for (size_t i = 0; i < valid_bones.size(); ++i) {
            Matrix4 inv = global_transforms[i].inverse();
            for(int j=0; j<16; ++j) {
                ibm_data.push_back(inv.m[j]);
            }
        }

        size_t ibm_offset = add_data(ibm_data.data(), ibm_data.size() * sizeof(float));
        if (bv_count > 0) bufferViews << ",";
        bufferViews << "{\"buffer\":0,\"byteOffset\":" << ibm_offset << ",\"byteLength\":" << (ibm_data.size() * sizeof(float)) << "}";
        int ibm_bv = bv_count++;

        if (acc_count > 0) accessors << ",";
        accessors << "{\"bufferView\":" << ibm_bv << ",\"componentType\":5126,\"count\":" << joint_indices.size() << ",\"type\":\"MAT4\"}";
        ibm_acc = acc_count++;

        skin_idx = 0;
    }

    for (size_t gi = 0; gi < geoms.size(); ++gi) {
        const auto& geom = geoms[gi];
        if (geom.positions.empty() || geom.indices.empty()) continue;

        size_t vcount = geom.positions.size() / 3;

        std::vector<float> positions_xyz;
        positions_xyz.reserve(geom.positions.size());
        for (size_t i = 0; i < vcount; ++i) {
            positions_xyz.push_back(geom.positions[i*3+0]);
            positions_xyz.push_back(geom.positions[i*3+1]);
            positions_xyz.push_back(geom.positions[i*3+2]);
        }

        std::vector<float> normals_xyz;
        normals_xyz.reserve(geom.normals.size());
        for (size_t i = 0; i < vcount; ++i) {
            normals_xyz.push_back(geom.normals[i*3+0]);
            normals_xyz.push_back(geom.normals[i*3+1]);
            normals_xyz.push_back(geom.normals[i*3+2]);
        }

        float pos_min[3] = {1e9f, 1e9f, 1e9f};
        float pos_max[3] = {-1e9f, -1e9f, -1e9f};
        for (size_t i = 0; i < vcount; ++i) {
            for (int j = 0; j < 3; ++j) {
                float v = positions_xyz[i*3+j];
                if (v < pos_min[j]) pos_min[j] = v;
                if (v > pos_max[j]) pos_max[j] = v;
            }
        }

        size_t pos_offset = add_data(positions_xyz.data(), positions_xyz.size() * sizeof(float));
        if (bv_count > 0) bufferViews << ",";
        bufferViews << "{\"buffer\":0,\"byteOffset\":" << pos_offset << ",\"byteLength\":" << (positions_xyz.size() * sizeof(float)) << ",\"target\":34962}";
        int pos_bv = bv_count++;

        if (acc_count > 0) accessors << ",";
        accessors << "{\"bufferView\":" << pos_bv << ",\"componentType\":5126,\"count\":" << vcount << ",\"type\":\"VEC3\"";
        accessors << ",\"min\":[" << pos_min[0] << "," << pos_min[1] << "," << pos_min[2] << "]";
        accessors << ",\"max\":[" << pos_max[0] << "," << pos_max[1] << "," << pos_max[2] << "]}";
        int pos_acc = acc_count++;

        size_t norm_offset = add_data(normals_xyz.data(), normals_xyz.size() * sizeof(float));
        if (bv_count > 0) bufferViews << ",";
        bufferViews << "{\"buffer\":0,\"byteOffset\":" << norm_offset << ",\"byteLength\":" << (normals_xyz.size() * sizeof(float)) << ",\"target\":34962}";
        int norm_bv = bv_count++;

        if (acc_count > 0) accessors << ",";
        accessors << "{\"bufferView\":" << norm_bv << ",\"componentType\":5126,\"count\":" << vcount << ",\"type\":\"VEC3\"}";
        int norm_acc = acc_count++;

        size_t uv_offset = add_data(geom.uvs.data(), geom.uvs.size() * sizeof(float));
        if (bv_count > 0) bufferViews << ",";
        bufferViews << "{\"buffer\":0,\"byteOffset\":" << uv_offset << ",\"byteLength\":" << (geom.uvs.size() * sizeof(float)) << ",\"target\":34962}";
        int uv_bv = bv_count++;

        if (acc_count > 0) accessors << ",";
        accessors << "{\"bufferView\":" << uv_bv << ",\"componentType\":5126,\"count\":" << (geom.uvs.size()/2) << ",\"type\":\"VEC2\"}";
        int uv_acc = acc_count++;

        int joints_acc = -1;
        int weights_acc = -1;

        if (!geom.bone_ids.empty() && !geom.bone_weights.empty() && !valid_bones.empty()) {
            std::vector<uint16_t> remapped_joints;
            remapped_joints.reserve(geom.bone_ids.size());

            for (size_t i = 0; i < geom.bone_ids.size(); ++i) {
                uint16_t orig_bone = geom.bone_ids[i];
                int node_idx = (orig_bone < original_to_node.size()) ? original_to_node[orig_bone] : -1;

                if (node_idx >= 0) {
                    remapped_joints.push_back((uint16_t)node_idx);
                } else {
                    remapped_joints.push_back(0);
                }
            }

            size_t joints_offset = add_data(remapped_joints.data(), remapped_joints.size() * sizeof(uint16_t));
            if (bv_count > 0) bufferViews << ",";
            bufferViews << "{\"buffer\":0,\"byteOffset\":" << joints_offset << ",\"byteLength\":" << (remapped_joints.size() * sizeof(uint16_t)) << ",\"target\":34962}";
            int joints_bv = bv_count++;

            if (acc_count > 0) accessors << ",";
            accessors << "{\"bufferView\":" << joints_bv << ",\"componentType\":5123,\"count\":" << (remapped_joints.size()/4) << ",\"type\":\"VEC4\"}";
            joints_acc = acc_count++;

            size_t weights_offset = add_data(geom.bone_weights.data(), geom.bone_weights.size() * sizeof(float));
            if (bv_count > 0) bufferViews << ",";
            bufferViews << "{\"buffer\":0,\"byteOffset\":" << weights_offset << ",\"byteLength\":" << (geom.bone_weights.size() * sizeof(float)) << ",\"target\":34962}";
            int weights_bv = bv_count++;

            if (acc_count > 0) accessors << ",";
            accessors << "{\"bufferView\":" << weights_bv << ",\"componentType\":5126,\"count\":" << (geom.bone_weights.size()/4) << ",\"type\":\"VEC4\"}";
            weights_acc = acc_count++;
        }

        size_t idx_offset = add_data(geom.indices.data(), geom.indices.size() * sizeof(uint32_t));
        if (bv_count > 0) bufferViews << ",";
        bufferViews << "{\"buffer\":0,\"byteOffset\":" << idx_offset << ",\"byteLength\":" << (geom.indices.size() * sizeof(uint32_t)) << ",\"target\":34963}";
        int idx_bv = bv_count++;

        if (acc_count > 0) accessors << ",";
        accessors << "{\"bufferView\":" << idx_bv << ",\"componentType\":5125,\"count\":" << geom.indices.size() << ",\"type\":\"SCALAR\"}";
        int idx_acc = acc_count++;

        std::string mesh_name = model_name + "_mesh_" + std::to_string(gi);

        if (gi < info.Meshes.size()) {
            if (!info.Meshes[gi].MeshName.empty()) {
                mesh_name = info.Meshes[gi].MeshName;
            }
        }

        if (mesh_count > 0) meshes << ",";
        meshes << "{\"name\":\"" << json_escape(mesh_name) << "\",\"primitives\":[";

        int tex_idx = -1;
        bool has_alpha = false;
        int this_mat_idx = -1;
        std::string material_name = "material_" + std::to_string(gi);

        if (!geom.diffuse_tex_name.empty()) {
            material_name = std::filesystem::path(geom.diffuse_tex_name).stem().string();

            std::vector<unsigned char> tex_buf;
            if (build_any_tex_buffer_for_name(geom.diffuse_tex_name, tex_buf)) {
                std::vector<uint8_t> png_data;
                if (decode_texture_to_png(tex_buf, png_data)) {
                    TexInfo tex_info;
                    if (parse_tex_info(tex_buf, tex_info)) {
                        has_alpha = (tex_info.PixelFormat == 39);
                    }

                    size_t img_offset = add_data(png_data.data(), png_data.size());

                    if (bv_count > 0) bufferViews << ",";
                    bufferViews << "{\"buffer\":0,\"byteOffset\":" << img_offset << ",\"byteLength\":" << png_data.size() << "}";
                    int img_bv = bv_count++;

                    if (img_count > 0) images << ",";
                    images << "{\"bufferView\":" << img_bv << ",\"mimeType\":\"image/png\"}";
                    int img_idx = img_count++;

                    if (tex_count > 0) textures << ",";
                    textures << "{\"source\":" << img_idx << "}";
                    tex_idx = tex_count++;
                }
            }

            if (mat_count > 0) materials << ",";
            materials << "{\"name\":\"" << json_escape(material_name) << "\"";
            materials << ",\"doubleSided\":true";
            if (has_alpha) {
                materials << ",\"alphaMode\":\"BLEND\"";
            }
            materials << ",\"pbrMetallicRoughness\":{";
            if (tex_idx >= 0) {
                materials << "\"baseColorTexture\":{\"index\":" << tex_idx << "},";
            }
            materials << "\"metallicFactor\":0.0,\"roughnessFactor\":0.9}}";
            this_mat_idx = mat_count++;
        }

        meshes << "{";
        meshes << "\"attributes\":{";
        meshes << "\"POSITION\":" << pos_acc << ",";
        meshes << "\"NORMAL\":" << norm_acc << ",";
        meshes << "\"TEXCOORD_0\":" << uv_acc;

        if (joints_acc >= 0 && weights_acc >= 0) {
            meshes << ",\"JOINTS_0\":" << joints_acc;
            meshes << ",\"WEIGHTS_0\":" << weights_acc;
        }

        meshes << "},";
        meshes << "\"indices\":" << idx_acc;
        if (this_mat_idx >= 0) {
            meshes << ",\"material\":" << this_mat_idx;
        }
        meshes << "}";

        meshes << "]}";
        mesh_count++;
    }

    int first_mesh_node = bone_node_count;

    int root_wrapper_node = bone_node_count + mesh_count;
    std::vector<int> wrapper_children = scene_root_bone_nodes;
    for (int i = 0; i < mesh_count; ++i) {
        wrapper_children.push_back(first_mesh_node + i);
    }

    json << "{";
    json << "\"asset\":{\"version\":\"2.0\",\"generator\":\"fable2_exporter\"},";
    json << "\"scene\":0,";
    json << "\"scenes\":[{\"nodes\":[" << root_wrapper_node << "]}],";

    json << "\"nodes\":[";
    if (!bone_nodes.str().empty()) {
        json << bone_nodes.str();
        json << ",";
    }
    for (int i = 0; i < mesh_count; ++i) {
        if (i > 0) json << ",";
        json << "{\"mesh\":" << i;
        if (skin_idx >= 0) {
            json << ",\"skin\":" << skin_idx;
        }
        json << "}";
    }
    json << ",{\"name\":\"Root\",\"rotation\":[-0.7071068,0,0,0.7071068],\"children\":[";
    for (size_t i = 0; i < wrapper_children.size(); ++i) {
        if (i > 0) json << ",";
        json << wrapper_children[i];
    }
    json << "]}";
    json << "],";

    json << "\"meshes\":[" << meshes.str() << "],";
    json << "\"buffers\":[{\"byteLength\":" << bin_data.size() << "}],";
    json << "\"bufferViews\":[" << bufferViews.str() << "],";
    json << "\"accessors\":[" << accessors.str() << "]";

    if (img_count > 0) {
        json << ",\"images\":[" << images.str() << "]";
        json << ",\"textures\":[" << textures.str() << "]";
    }

    if (mat_count > 0) {
        json << ",\"materials\":[" << materials.str() << "]";
    }

    if (skin_idx >= 0 && ibm_acc >= 0) {
        json << ",\"skins\":[{\"inverseBindMatrices\":" << ibm_acc;
        json << ",\"joints\":[";
        for (size_t i = 0; i < joint_indices.size(); ++i) {
            if (i > 0) json << ",";
            json << joint_indices[i];
        }
        json << "]}]";
    }

    json << "}";

    std::string json_str = json.str();
    while (json_str.size() & 3) json_str.push_back(' ');
    uint32_t json_len = (uint32_t)json_str.size();

    while (bin_data.size() & 3) bin_data.push_back(0);
    uint32_t bin_len = (uint32_t)bin_data.size();

    uint32_t total = 12 + 8 + json_len + 8 + bin_len;

    std::error_code ec;
    auto parent = std::filesystem::path(glb_path).parent_path();
    if(!parent.empty()) std::filesystem::create_directories(parent, ec);

    std::ofstream out(glb_path, std::ios::binary|std::ios::trunc);
    if (!out) {
        err_msg = "Failed to open output file: " + glb_path;
        return false;
    }

    auto w32=[&](uint32_t v){ out.write(reinterpret_cast<const char*>(&v), 4); };

    w32(0x46546C67); w32(2); w32(total);
    w32(json_len); w32(0x4E4F534A);
    out.write(json_str.data(), (std::streamsize)json_str.size());
    w32(bin_len); w32(0x004E4942);
    out.write((const char*)bin_data.data(), (std::streamsize)bin_data.size());

    if (!out.good()){
        err_msg = "GLB write failed";
        return false;
    }
    return true;
}