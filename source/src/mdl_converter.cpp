#include "mdl_converter.h"
#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <array>
#include <cstdio>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace {

// -------- small helpers --------
static inline std::string hexu(uint64_t v){
    std::ostringstream oss; oss << "0x" << std::hex << std::uppercase << v; return oss.str();
}
static inline std::string hexdump_around(const uint8_t* data, size_t size, size_t off, size_t bytes=16){
    if(!data || size==0) return "(no-bytes)";
    size_t start = (off > bytes/2) ? off - bytes/2 : 0;
    size_t end   = std::min(size, off + bytes/2);
    std::ostringstream oss;
    oss << "[" << hexu(start) << ":" << hexu(end) << "] ";
    for(size_t i=start;i<end;++i){
        oss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (unsigned)(data[i]);
        if(i+1<end) oss << " ";
    }
    return oss.str();
}

// -------- Reader (matches convert.py semantics closely) --------
struct Reader {
    const uint8_t* data = nullptr;
    size_t size = 0;
    size_t off = 0;
    bool be = true;

    bool need(size_t n) const { return off + n <= size; }

    bool rbytes(void* dst, size_t n) {
        if (!need(n)) return false;
        std::memcpy(dst, data + off, n);
        off += n;
        return true;
    }

    bool ru32(uint32_t& out){
        if (!need(4)) return false;
        const uint8_t* p = data + off; off += 4;
        if (be) out = (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|uint32_t(p[3]);
        else     out = (uint32_t(p[3])<<24)|(uint32_t(p[2])<<16)|(uint32_t(p[1])<<8)|uint32_t(p[0]);
        return true;
    }

    // reinterpret u32 as f32 (like convert.py ru32_as_rf32_arr)
    bool ru32_as_f32(float& f){
        uint32_t u; if(!ru32(u)) return false;
        std::memcpy(&f, &u, 4);
        return true;
    }

    // Match Python rstr(): read until NUL or maxlen; return what we collected (NEVER fail for missing NUL).
    bool rstr(std::string& s, size_t maxlen=8192){
        s.clear();
        size_t start = off;
        size_t limit = std::min(size, start + maxlen);
        while (off < limit) {
            char c = (char)data[off++];
            if (c == '\0') return true; // found NUL
            s.push_back(c);
        }
        // No NUL inside maxlen/EOF -> Python returns whatever it got. Treat as success.
        return true;
    }

    bool skip_u32s(size_t k){
        size_t n = k * 4;
        if(!need(n)) return false;
        off += n; return true;
    }
};

// -------- bone holder --------
struct Bone {
    std::string name;
    int original_index = -1;
    int parent_original = -1;
    bool has_tf = false;
    float rotation[4]   = {0,0,0,1}; // x,y,z,w
    float translation[3]= {0,0,0};
    float scale[3]      = {1,1,1};
    std::vector<int> children;       // condensed indices
};

static std::string json_escape(const std::string& s){
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

// Minimal GLB writer: JSON chunk only (valid for skeleton-only)
static bool write_glb_json_only(const std::string& json, const std::string& out_glb, std::string& err){
    const uint32_t MAGIC = 0x46546C67; // 'glTF'
    const uint32_t VER   = 2;
    const uint32_t JSONt = 0x4E4F534A; // 'JSON'

    std::string json_padded = json;
    while (json_padded.size() & 3) json_padded.push_back(' ');
    uint32_t json_len = (uint32_t)json_padded.size();
    uint32_t total = 12 + 8 + json_len;

    std::error_code ec;
    auto parent = std::filesystem::path(out_glb).parent_path();
    if(!parent.empty()) std::filesystem::create_directories(parent, ec);

    std::ofstream out(out_glb, std::ios::binary|std::ios::trunc);
    if (!out) { err = "GLB open failed: " + out_glb; return false; }

    auto w32=[&](uint32_t v){ out.write(reinterpret_cast<const char*>(&v), 4); };

    w32(MAGIC); w32(VER); w32(total);
    w32(json_len); w32(JSONt);
    out.write(json_padded.data(), (std::streamsize)json_padded.size());
    if (!out.good()){ err = "GLB write failed (JSON)."; return false; }
    out.flush();
    if (!out.good()){ err = "GLB flush failed."; return false; }
    return true;
}

} // namespace

bool mdl_to_glb_file_ex(const std::string& mdl_path,
                        const std::string& glb_path,
                        std::string& err_msg)
{
    auto fail = [&](const std::string& where, const std::string& extra)->bool{
        err_msg = "[mdl_to_glb] " + where + " | " + extra;
        return false;
    };

    err_msg.clear();

    // Read whole file
    std::ifstream f(mdl_path, std::ios::binary);
    if (!f) return fail("open", "mdl=" + mdl_path);
    f.seekg(0, std::ios::end);
    std::streamoff sz = f.tellg();
    if (sz <= 0) return fail("size", "empty mdl");
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf((size_t)sz);
    if (!f.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)buf.size())) {
        return fail("read", "wanted=" + std::to_string(buf.size()));
    }

    Reader r; r.data = buf.data(); r.size = buf.size(); r.be = true; r.off = 0;

    // === EXACTLY mirror convert.py's skip_to_mesh_headers bone part ===
    if (!r.skip_u32s(8)) {
        return fail("skip_u32s(8)", "off=" + hexu(r.off) + " size=" + std::to_string(r.size));
    }

    uint32_t bone_count = 0;
    if (!r.ru32(bone_count)) {
        return fail("bone_count", "off=" + hexu(r.off) + " bytes=" + hexdump_around(r.data, r.size, r.off));
    }
    if (bone_count > 50000u) { // sanity guard
        return fail("bone_count_range", "bone_count=" + std::to_string(bone_count));
    }

    std::vector<std::string> names; names.resize(bone_count);
    std::vector<int> parent_ids(bone_count, -1);

    for (uint32_t i=0; i<bone_count; ++i) {
        size_t name_off = r.off;
        std::string nm;
        r.rstr(nm); // NEVER fail just because no NUL — matches Python
        uint32_t pid = 0;
        if (!r.ru32(pid)) {
            return fail("bone_parent", "i="+std::to_string(i)+" off="+hexu(r.off)+" around="+hexdump_around(r.data, r.size, r.off));
        }
        names[i] = nm;
        parent_ids[i] = (pid == 0xFFFFFFFFu) ? -1 : (int)pid;
        (void)name_off;
    }

    uint32_t bt_count = 0;
    if (!r.ru32(bt_count)) {
        return fail("bt_count", "off=" + hexu(r.off) + " bytes=" + hexdump_around(r.data, r.size, r.off));
    }

    // 11 floats per bone if counts match (quat[4], pos[3], scale[3], extra[1])
    std::vector<std::array<float,11>> bone_tf;
    bone_tf.resize(bone_count);
    const bool have_tf = (bt_count == bone_count && bone_count > 0);

    if (have_tf) {
        for (uint32_t i=0; i<bt_count; ++i) {
            float arr[11];
            for (int k=0; k<11; ++k) {
                if (!r.ru32_as_f32(arr[k])) {
                    return fail("bone_tf", "i="+std::to_string(i)+" k="+std::to_string(k)+" off="+hexu(r.off)+" bytes="+hexdump_around(r.data, r.size, r.off));
                }
            }
            for (int k=0; k<11; ++k) bone_tf[i][k] = arr[k];
        }
    } else {
        // Follow convert.py fallback: skip up to 44 * min(bt_count, 65535)
        uint32_t m = bt_count;
        if (m > 65535u) m = 65535u;
        size_t bytes = (size_t)m * 44;
        if (!r.need(bytes)) {
            return fail("skip_tf_mismatch", "need="+std::to_string(bytes)+" off="+hexu(r.off)+" size="+std::to_string(r.size));
        }
        r.off += bytes;
    }

    // === Build condensed bones (exclude "Rig_Asset") ===
    std::vector<int> orig_to_node(bone_count, -1);
    std::vector<Bone> nodes; nodes.reserve(bone_count);

    for (uint32_t i=0; i<bone_count; ++i) {
        const std::string& nm = names[i];
        if (nm.find("Rig_Asset") != std::string::npos) continue; // match Python filter
        Bone b;
        b.name = nm;
        b.original_index = (int)i;
        b.parent_original = parent_ids[i];
        if (have_tf) {
            const auto& tf = bone_tf[i];
            b.rotation[0]=tf[0]; b.rotation[1]=tf[1]; b.rotation[2]=tf[2]; b.rotation[3]=tf[3];
            b.translation[0]=tf[4]; b.translation[1]=tf[5]; b.translation[2]=tf[6];
            b.scale[0]=tf[7]; b.scale[1]=tf[8]; b.scale[2]=tf[9];
            b.has_tf = true;
        }
        orig_to_node[i] = (int)nodes.size();
        nodes.push_back(std::move(b));
    }

    if (nodes.empty()) {
        return fail("no_bones_after_filter", "bone_count="+std::to_string(bone_count));
    }

    // parent/child relationships using condensed indices
    std::vector<int> roots;
    for (size_t ni=0; ni<nodes.size(); ++ni) {
        int p_orig = nodes[ni].parent_original;
        if (p_orig >= 0 && p_orig < (int)bone_count) {
            int p_node = orig_to_node[p_orig];
            if (p_node >= 0 && p_node != (int)ni) {
                nodes[p_node].children.push_back((int)ni);
                continue;
            }
        }
        roots.push_back((int)ni);
    }

    // === Build minimal glTF JSON ===
    std::string json;
    json += "{";
    json += "\"asset\":{\"version\":\"2.0\",\"generator\":\"mdl_converter_cpp_skeleton\"},";
    json += "\"scene\":0,";
    json += "\"scenes\":[{\"nodes\":[";
    if (!roots.empty()) {
        for (size_t i=0;i<roots.size();++i){ if(i) json += ","; json += std::to_string(roots[i]); }
    } else {
        for (size_t i=0;i<nodes.size();++i){ if(i) json += ","; json += std::to_string((int)i); }
    }
    json += "]}],";

    // nodes
    json += "\"nodes\":[";
    for (size_t i=0;i<nodes.size();++i) {
        const auto& n = nodes[i];
        if (i) json += ",";
        json += "{";
        if (!n.name.empty()) {
            json += "\"name\":\""+json_escape(n.name)+"\",";
        }
        if (!n.children.empty()) {
            json += "\"children\":[";
            for (size_t k=0;k<n.children.size();++k){ if(k) json += ","; json += std::to_string(n.children[k]); }
            json += "],";
        }
        if (n.has_tf) {
            json += "\"rotation\":["
                    + std::to_string((double)n.rotation[0])+","
                    + std::to_string((double)n.rotation[1])+","
                    + std::to_string((double)n.rotation[2])+","
                    + std::to_string((double)n.rotation[3])+"]";
            json += ",\"translation\":["
                    + std::to_string((double)n.translation[0])+","
                    + std::to_string((double)n.translation[1])+","
                    + std::to_string((double)n.translation[2])+"]";
            json += ",\"scale\":["
                    + std::to_string((double)n.scale[0])+","
                    + std::to_string((double)n.scale[1])+","
                    + std::to_string((double)n.scale[2])+"]";
        } else {
            json += "\"rotation\":[0,0,0,1]";
        }
        json += "}";
    }
    json += "]"; // nodes
    json += "}"; // root glTF object

    std::string werr;
    if (!write_glb_json_only(json, glb_path, werr)) {
        return fail("glb_write", werr + " | out=" + glb_path);
    }

    // verify
    std::error_code ec;
    auto ok = std::filesystem::exists(glb_path, ec) && std::filesystem::file_size(glb_path, ec) > 0;
    if (!ok) {
        return fail("glb_verify", "exists=" + std::to_string(std::filesystem::exists(glb_path, ec)) +
                                   " size=" + std::to_string(std::filesystem::file_size(glb_path, ec)));
    }

    return true;
}
