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
#include <unordered_map>
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

static void align_to_4(std::vector<uint8_t>& buf) {
    while (buf.size() & 3) buf.push_back(0);
}

struct GLBWriter {
    std::vector<uint8_t> bin_data;
    std::string json;
    std::ostringstream bufferViews_json;
    std::ostringstream accessors_json;
    std::ostringstream images_json;
    std::ostringstream textures_json;
    std::ostringstream materials_json;

    int bv_count = 0;
    int acc_count = 0;
    int img_count = 0;
    int tex_count = 0;
    int mat_count = 0;

    size_t add_buffer_data(const void* data, size_t size) {
        size_t offset = bin_data.size();
        const uint8_t* p = (const uint8_t*)data;
        bin_data.insert(bin_data.end(), p, p + size);
        align_to_4(bin_data);
        return offset;
    }

    int add_buffer_view(size_t offset, size_t length, int target = 0) {
        if (bv_count > 0) bufferViews_json << ",";
        bufferViews_json << "{\"buffer\":0,\"byteOffset\":" << offset << ",\"byteLength\":" << length;
        if (target > 0) bufferViews_json << ",\"target\":" << target;
        bufferViews_json << "}";
        return bv_count++;
    }

    int add_accessor(int bufferView, int componentType, int count, const std::string& type,
                     const float* min_vals = nullptr, const float* max_vals = nullptr, int dims = 3) {
        if (acc_count > 0) accessors_json << ",";
        accessors_json << "{\"bufferView\":" << bufferView
                      << ",\"componentType\":" << componentType
                      << ",\"count\":" << count
                      << ",\"type\":\"" << type << "\"";
        if (min_vals && max_vals) {
            accessors_json << ",\"min\":[";
            for (int i = 0; i < dims; ++i) {
                if (i) accessors_json << ",";
                accessors_json << min_vals[i];
            }
            accessors_json << "],\"max\":[";
            for (int i = 0; i < dims; ++i) {
                if (i) accessors_json << ",";
                accessors_json << max_vals[i];
            }
            accessors_json << "]";
        }
        accessors_json << "}";
        return acc_count++;
    }

    int add_image(size_t offset, size_t length, const std::string& mimeType) {
        if (img_count > 0) images_json << ",";
        int bv = add_buffer_view(offset, length);
        images_json << "{\"bufferView\":" << bv << ",\"mimeType\":\"" << mimeType << "\"}";
        return img_count++;
    }

    int add_texture(int source) {
        if (tex_count > 0) textures_json << ",";
        textures_json << "{\"source\":" << source << "}";
        return tex_count++;
    }

    int add_material(int baseColorTexture = -1) {
        if (mat_count > 0) materials_json << ",";
        materials_json << "{\"pbrMetallicRoughness\":{";
        if (baseColorTexture >= 0) {
            materials_json << "\"baseColorTexture\":{\"index\":" << baseColorTexture << "},";
        }
        materials_json << "\"metallicFactor\":0.0,\"roughnessFactor\":0.9}}";
        return mat_count++;
    }

    bool write(const std::string& path, std::string& err) {
        const uint32_t MAGIC = 0x46546C67;
        const uint32_t VER = 2;
        const uint32_t JSONt = 0x4E4F534A;
        const uint32_t BINt = 0x004E4942;

        std::string json_padded = json;
        while (json_padded.size() & 3) json_padded.push_back(' ');
        uint32_t json_len = (uint32_t)json_padded.size();

        align_to_4(bin_data);
        uint32_t bin_len = (uint32_t)bin_data.size();

        uint32_t total = 12 + 8 + json_len;
        if (bin_len > 0) total += 8 + bin_len;

        std::error_code ec;
        auto parent = std::filesystem::path(path).parent_path();
        if(!parent.empty()) std::filesystem::create_directories(parent, ec);

        std::ofstream out(path, std::ios::binary|std::ios::trunc);
        if (!out) { err = "GLB open failed: " + path; return false; }

        auto w32=[&](uint32_t v){ out.write(reinterpret_cast<const char*>(&v), 4); };

        w32(MAGIC); w32(VER); w32(total);
        w32(json_len); w32(JSONt);
        out.write(json_padded.data(), (std::streamsize)json_padded.size());

        if (bin_len > 0) {
            w32(bin_len); w32(BINt);
            out.write((const char*)bin_data.data(), (std::streamsize)bin_data.size());
        }

        if (!out.good()){ err = "GLB write failed."; return false; }
        return true;
    }
};

struct Bone {
    std::string name;
    int original_index = -1;
    int parent_original = -1;
    bool has_tf = false;
    float rotation[4] = {0,0,0,1};
    float translation[3] = {0,0,0};
    float scale[3] = {1,1,1};
    std::vector<int> children;
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

    if (tex_info.PixelFormat != 35) return false;

    size_t bx = (size_t)((w+3)/4), by = (size_t)((h+3)/4);
    size_t sz_bc1 = bx*by*8;

    if (mip.MipDataSizeParsed < sz_bc1) return false;

    std::vector<uint8_t> rgba((size_t)w*(size_t)h*4, 0xFF);
    const uint8_t* src = tex_buf.data() + mip.MipDataOffset;
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
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
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
    idat_compressed.push_back(0x78);
    idat_compressed.push_back(0x01);

    size_t block_size = 65535;
    for (size_t pos = 0; pos < idat_raw.size(); pos += block_size) {
        size_t chunk = std::min(block_size, idat_raw.size() - pos);
        bool last = (pos + chunk >= idat_raw.size());

        idat_compressed.push_back(last ? 0x01 : 0x00);
        idat_compressed.push_back(chunk & 0xFF);
        idat_compressed.push_back((chunk >> 8) & 0xFF);
        idat_compressed.push_back((~chunk) & 0xFF);
        idat_compressed.push_back(((~chunk) >> 8) & 0xFF);

        idat_compressed.insert(idat_compressed.end(),
                              idat_raw.begin() + pos,
                              idat_raw.begin() + pos + chunk);
    }

    uint32_t adler = 1;
    uint32_t s1 = 1, s2 = 0;
    for (uint8_t byte : idat_raw) {
        s1 = (s1 + byte) % 65521;
        s2 = (s2 + s1) % 65521;
    }
    adler = (s2 << 16) | s1;

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
                     std::string& err_msg)
{
    err_msg.clear();

    MDLInfo info;
    if (!parse_mdl_info(mdl_data, info)) {
        err_msg = "Failed to parse MDL info";
        return false;
    }

    std::vector<MDLMeshGeom> geoms;
    if (!parse_mdl_geometry(mdl_data, info, geoms)) {
        err_msg = "Failed to parse MDL geometry";
        return false;
    }

    GLBWriter writer;

    std::vector<int> orig_to_node(info.BoneCount, -1);
    std::vector<Bone> nodes;
    nodes.reserve(info.BoneCount);

    for (size_t i = 0; i < info.Bones.size(); ++i) {
        const auto& b = info.Bones[i];
        if (b.Name.find("Rig_Asset") != std::string::npos) continue;

        Bone bone;
        bone.name = b.Name;
        bone.original_index = (int)i;
        bone.parent_original = b.ParentID;
        bone.has_tf = false;

        orig_to_node[i] = (int)nodes.size();
        nodes.push_back(std::move(bone));
    }

    std::vector<int> roots;
    for (size_t ni = 0; ni < nodes.size(); ++ni) {
        int p_orig = nodes[ni].parent_original;
        if (p_orig >= 0 && p_orig < (int)info.Bones.size()) {
            int p_node = orig_to_node[p_orig];
            if (p_node >= 0 && p_node != (int)ni) {
                nodes[p_node].children.push_back((int)ni);
                continue;
            }
        }
        roots.push_back((int)ni);
    }

    std::ostringstream meshes_json;
    int mesh_count = 0;

    for (size_t gi = 0; gi < geoms.size(); ++gi) {
        const auto& geom = geoms[gi];
        if (geom.positions.empty() || geom.indices.empty()) continue;

        size_t vcount = geom.positions.size() / 3;

        float pos_min[3] = {1e9f, 1e9f, 1e9f};
        float pos_max[3] = {-1e9f, -1e9f, -1e9f};
        for (size_t i = 0; i < vcount; ++i) {
            for (int j = 0; j < 3; ++j) {
                float v = geom.positions[i*3+j];
                if (v < pos_min[j]) pos_min[j] = v;
                if (v > pos_max[j]) pos_max[j] = v;
            }
        }

        size_t pos_offset = writer.add_buffer_data(geom.positions.data(), geom.positions.size() * sizeof(float));
        int pos_bv = writer.add_buffer_view(pos_offset, geom.positions.size() * sizeof(float), 34962);
        int pos_acc = writer.add_accessor(pos_bv, 5126, (int)vcount, "VEC3", pos_min, pos_max, 3);

        size_t norm_offset = writer.add_buffer_data(geom.normals.data(), geom.normals.size() * sizeof(float));
        int norm_bv = writer.add_buffer_view(norm_offset, geom.normals.size() * sizeof(float), 34962);
        int norm_acc = writer.add_accessor(norm_bv, 5126, (int)vcount, "VEC3");

        size_t uv_offset = writer.add_buffer_data(geom.uvs.data(), geom.uvs.size() * sizeof(float));
        int uv_bv = writer.add_buffer_view(uv_offset, geom.uvs.size() * sizeof(float), 34962);
        int uv_acc = writer.add_accessor(uv_bv, 5126, (int)(geom.uvs.size()/2), "VEC2");

        size_t idx_offset = writer.add_buffer_data(geom.indices.data(), geom.indices.size() * sizeof(uint32_t));
        int idx_bv = writer.add_buffer_view(idx_offset, geom.indices.size() * sizeof(uint32_t), 34963);
        int idx_acc = writer.add_accessor(idx_bv, 5125, (int)geom.indices.size(), "SCALAR");

        int mat_idx = -1;
        if (gi < info.Meshes.size() && !info.Meshes[gi].Materials.empty()) {
            const auto& mat = info.Meshes[gi].Materials[0];

            int tex_idx = -1;
            if (!mat.TextureName.empty()) {
                std::vector<unsigned char> tex_buf;
                if (build_any_tex_buffer_for_name(mat.TextureName, tex_buf)) {
                    std::vector<uint8_t> png_data;
                    if (decode_texture_to_png(tex_buf, png_data)) {
                        size_t img_offset = writer.add_buffer_data(png_data.data(), png_data.size());
                        int img_idx = writer.add_image(img_offset, png_data.size(), "image/png");
                        tex_idx = writer.add_texture(img_idx);
                    }
                }
            }

            mat_idx = writer.add_material(tex_idx);
        }

        if (mesh_count > 0) meshes_json << ",";
        meshes_json << "{\"primitives\":[{";
        meshes_json << "\"attributes\":{";
        meshes_json << "\"POSITION\":" << pos_acc << ",";
        meshes_json << "\"NORMAL\":" << norm_acc << ",";
        meshes_json << "\"TEXCOORD_0\":" << uv_acc;
        meshes_json << "},";
        meshes_json << "\"indices\":" << idx_acc;
        if (mat_idx >= 0) {
            meshes_json << ",\"material\":" << mat_idx;
        }
        meshes_json << "}]}";
        mesh_count++;
    }

    writer.json = "{";
    writer.json += "\"asset\":{\"version\":\"2.0\",\"generator\":\"fable2_mdl_exporter\"},";
    writer.json += "\"scene\":0,";
    writer.json += "\"scenes\":[{\"nodes\":[";

    if (!roots.empty()) {
        for (size_t i = 0; i < roots.size(); ++i) {
            if (i) writer.json += ",";
            writer.json += std::to_string(roots[i]);
        }
    }
    writer.json += "]}],";

    writer.json += "\"nodes\":[";
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& n = nodes[i];
        if (i) writer.json += ",";
        writer.json += "{";
        if (!n.name.empty()) {
            writer.json += "\"name\":\"" + json_escape(n.name) + "\",";
        }
        if (!n.children.empty()) {
            writer.json += "\"children\":[";
            for (size_t k = 0; k < n.children.size(); ++k) {
                if (k) writer.json += ",";
                writer.json += std::to_string(n.children[k]);
            }
            writer.json += "],";
        }
        writer.json += "\"rotation\":[0,0,0,1]";
        writer.json += "}";
    }

    for (int i = 0; i < mesh_count; ++i) {
        writer.json += ",{\"mesh\":" + std::to_string(i) + "}";
    }

    writer.json += "],";

    if (mesh_count > 0) {
        writer.json += "\"meshes\":[" + meshes_json.str() + "],";
    }

    writer.json += "\"buffers\":[{\"byteLength\":" + std::to_string(writer.bin_data.size()) + "}],";
    writer.json += "\"bufferViews\":[" + writer.bufferViews_json.str() + "],";
    writer.json += "\"accessors\":[" + writer.accessors_json.str() + "]";

    if (writer.img_count > 0) {
        writer.json += ",\"images\":[" + writer.images_json.str() + "]";
        writer.json += ",\"textures\":[" + writer.textures_json.str() + "]";
    }

    if (writer.mat_count > 0) {
        writer.json += ",\"materials\":[" + writer.materials_json.str() + "]";
    }

    writer.json += "}";

    std::string werr;
    if (!writer.write(glb_path, werr)) {
        err_msg = werr;
        return false;
    }

    std::error_code ec;
    bool ok = std::filesystem::exists(glb_path, ec) && std::filesystem::file_size(glb_path, ec) > 0;
    if (!ok) {
        err_msg = "GLB verify failed";
        return false;
    }

    return true;
}