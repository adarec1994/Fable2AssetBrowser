#include "LevelExport.h"

#include "LevelLoader.h"
#include "EhfChunkParser.h"
#include "../MDL/ModelParser.h"
#include "../MDL/mdl_converter.h"
#include "../MDL/MdlFbxExport.h"
#include "../MDL/MdlTexExport.h"
#include "../textures/TexParser.h"
#include "../textures/export/TextureExport.h"
#include "../Utilities/Progress.h"
#include "../UI/OutputLog.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Level {
namespace {

std::atomic<bool> g_level_exporting{false};

struct ExportOnlyLevelLoadGuard {
    bool previous = false;

    ExportOnlyLevelLoadGuard()
        : previous(g_level_export_only_load.exchange(true))
    {
        g_pending_terrain_load.store(false);
    }

    ~ExportOnlyLevelLoadGuard()
    {
        g_pending_terrain_load.store(false);
        g_level_export_only_load.store(previous);
    }
};

std::string json_escape(const std::string& s)
{
    std::ostringstream os;
    for (unsigned char c : s) {
        switch (c) {
            case '"':  os << "\\\""; break;
            case '\\': os << "\\\\"; break;
            case '\b': os << "\\b";  break;
            case '\f': os << "\\f";  break;
            case '\n': os << "\\n";  break;
            case '\r': os << "\\r";  break;
            case '\t': os << "\\t";  break;
            default:
                if (c < 0x20) {
                    os << "\\u" << std::hex << std::setw(4)
                       << std::setfill('0') << int(c)
                       << std::dec << std::setfill(' ');
                } else {
                    os << char(c);
                }
                break;
        }
    }
    return os.str();
}

std::string to_slash(std::string s)
{
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

std::string lower_copy(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

uint64_t fnv1a64(const std::string& s)
{
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= uint64_t(c);
        h *= 1099511628211ull;
    }
    return h;
}

std::string hex8(uint64_t v)
{
    std::ostringstream os;
    os << std::hex << std::setw(8) << std::setfill('0')
       << uint32_t(v & 0xffffffffu);
    return os.str();
}

std::string sanitize_name(std::string s)
{
    if (s.empty()) s = "export";
    for (char& c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 32 || c == '<' || c == '>' || c == ':' || c == '"' ||
            c == '/' || c == '\\' || c == '|' || c == '?' || c == '*') {
            c = '_';
        }
    }
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) s.pop_back();
    return s.empty() ? std::string("export") : s;
}

std::vector<std::string> path_parts(const std::string& p)
{
    std::vector<std::string> parts;
    std::string cur;
    for (char c : p) {
        if (c == '/' || c == '\\') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

std::string stem_of(std::string p)
{
    std::replace(p.begin(), p.end(), '\\', '/');
    const size_t slash = p.find_last_of('/');
    std::string leaf = (slash == std::string::npos) ? p : p.substr(slash + 1);
    const size_t dot = leaf.find_last_of('.');
    if (dot != std::string::npos) leaf.resize(dot);
    return leaf.empty() ? std::string("asset") : leaf;
}

std::string level_folder_name(const FlatAssetEntry& entry)
{
    const auto parts = path_parts(entry.full_path);
    std::string stem = stem_of(entry.name.empty() ? entry.full_path
                                                  : entry.name);
    const std::string low = lower_copy(stem);
    if ((low == "defaultscenario" || low == "chapter1" ||
         low == "chapter2" || low == "chapter3" || low == "chapter4") &&
        parts.size() >= 3) {
        const std::string parent = lower_copy(parts[parts.size() - 2]);
        if (parent == low || parent == "defaultscenario") {
            stem = parts[parts.size() - 3];
        }
    }
    return sanitize_name(stem);
}

bool write_bytes(const std::filesystem::path& path,
                 const std::vector<uint8_t>& bytes,
                 std::string& err)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        err = "cannot open " + path.string();
        return false;
    }
    if (!bytes.empty()) {
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }
    if (!f.good()) {
        err = "write failed for " + path.string();
        return false;
    }
    return true;
}

bool file_exists_nonempty(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
    return std::filesystem::file_size(path, ec) > 0 && !ec;
}

bool write_text(const std::filesystem::path& path,
                const std::string& text,
                std::string& err)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        err = "cannot open " + path.string();
        return false;
    }
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f.good()) {
        err = "write failed for " + path.string();
        return false;
    }
    return true;
}

void append_raw(std::vector<uint8_t>& out, const void* data, size_t size)
{
    const auto* p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + size);
}

size_t append_aligned(std::vector<uint8_t>& out, const void* data, size_t size)
{
    const size_t off = out.size();
    append_raw(out, data, size);
    while (out.size() & 3u) out.push_back(0);
    return off;
}

void put_u32(std::ofstream& out, uint32_t v)
{
    out.write(reinterpret_cast<const char*>(&v), 4);
}

bool write_terrain_glb(const TerrainMesh& mesh,
                       const std::filesystem::path& path,
                       std::string& err)
{
    if (!mesh.ok || mesh.positions.empty() || mesh.indices.empty()) {
        err = "terrain mesh is empty";
        return false;
    }

    std::vector<uint8_t> bin;
    std::ostringstream views;
    std::ostringstream accessors;
    int view_count = 0;
    int acc_count = 0;

    auto add_view = [&](const void* data,
                        size_t bytes,
                        int target) -> int {
        const size_t off = append_aligned(bin, data, bytes);
        if (view_count > 0) views << ",";
        views << "{\"buffer\":0,\"byteOffset\":" << off
              << ",\"byteLength\":" << bytes;
        if (target) views << ",\"target\":" << target;
        views << "}";
        return view_count++;
    };

    auto add_float_accessor = [&](const std::vector<float>& data,
                                  int comps,
                                  const char* type,
                                  bool bounds) -> int {
        const int view = add_view(data.data(), data.size() * sizeof(float),
                                  34962);
        if (acc_count > 0) accessors << ",";
        accessors << "{\"bufferView\":" << view
                  << ",\"componentType\":5126,\"count\":"
                  << (data.size() / size_t(comps))
                  << ",\"type\":\"" << type << "\"";
        if (bounds && data.size() >= size_t(comps)) {
            std::vector<float> mn(comps, std::numeric_limits<float>::max());
            std::vector<float> mx(comps, -std::numeric_limits<float>::max());
            for (size_t i = 0; i + comps <= data.size(); i += comps) {
                for (int c = 0; c < comps; ++c) {
                    mn[c] = std::min(mn[c], data[i + c]);
                    mx[c] = std::max(mx[c], data[i + c]);
                }
            }
            accessors << ",\"min\":[";
            for (int c = 0; c < comps; ++c) {
                if (c) accessors << ",";
                accessors << mn[c];
            }
            accessors << "],\"max\":[";
            for (int c = 0; c < comps; ++c) {
                if (c) accessors << ",";
                accessors << mx[c];
            }
            accessors << "]";
        }
        accessors << "}";
        return acc_count++;
    };

    std::vector<float> terrain_uv01;
    const size_t vertex_count = mesh.positions.size() / 3;
    if (mesh.width > 1 && mesh.height > 1 &&
        vertex_count == size_t(mesh.width) * size_t(mesh.height)) {
        terrain_uv01.resize(vertex_count * 2);
        for (uint32_t y = 0; y < mesh.height; ++y) {
            for (uint32_t x = 0; x < mesh.width; ++x) {
                const size_t i = size_t(y) * mesh.width + x;
                terrain_uv01[i * 2 + 0] =
                    float(x) / float(mesh.width - 1);
                terrain_uv01[i * 2 + 1] =
                    float(y) / float(mesh.height - 1);
            }
        }
    }

    const int pos_acc = add_float_accessor(mesh.positions, 3, "VEC3", true);
    int norm_acc = -1;
    int uv_acc = -1;
    int uv_weight_acc = -1;
    if (mesh.normals.size() / 3 == mesh.positions.size() / 3) {
        norm_acc = add_float_accessor(mesh.normals, 3, "VEC3", false);
    }
    if (mesh.uvs.size() / 2 == mesh.positions.size() / 3) {
        uv_acc = add_float_accessor(mesh.uvs, 2, "VEC2", false);
    }
    if (!terrain_uv01.empty()) {
        uv_weight_acc = add_float_accessor(terrain_uv01, 2, "VEC2", false);
    }

    const int idx_view = add_view(mesh.indices.data(),
                                  mesh.indices.size() * sizeof(uint32_t),
                                  34963);
    if (acc_count > 0) accessors << ",";
    accessors << "{\"bufferView\":" << idx_view
              << ",\"componentType\":5125,\"count\":" << mesh.indices.size()
              << ",\"type\":\"SCALAR\"}";
    const int idx_acc = acc_count++;

    std::ostringstream json;
    json << std::setprecision(9);
    json << "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Fable2AssetBrowser\"},";
    json << "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],";
    json << "\"nodes\":[{\"name\":\"terrain\",\"mesh\":0}],";
    json << "\"materials\":[{\"name\":\"Fable terrain shader data in .fable\",";
    json << "\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,1,1,1],";
    json << "\"roughnessFactor\":1,\"metallicFactor\":0}}],";
    json << "\"meshes\":[{\"name\":\"terrain\",\"primitives\":[{\"attributes\":{";
    json << "\"POSITION\":" << pos_acc;
    if (norm_acc >= 0) json << ",\"NORMAL\":" << norm_acc;
    if (uv_acc >= 0) json << ",\"TEXCOORD_0\":" << uv_acc;
    if (uv_weight_acc >= 0) json << ",\"TEXCOORD_1\":" << uv_weight_acc;
    json << "},\"indices\":" << idx_acc << ",\"material\":0}]}],";
    json << "\"buffers\":[{\"byteLength\":" << bin.size() << "}],";
    json << "\"bufferViews\":[" << views.str() << "],";
    json << "\"accessors\":[" << accessors.str() << "]}";

    std::string json_str = json.str();
    while (json_str.size() & 3u) json_str.push_back(' ');
    while (bin.size() & 3u) bin.push_back(0);

    const uint32_t json_len = static_cast<uint32_t>(json_str.size());
    const uint32_t bin_len = static_cast<uint32_t>(bin.size());
    const uint32_t total_len = 12 + 8 + json_len + 8 + bin_len;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        err = "cannot open " + path.string();
        return false;
    }
    put_u32(out, 0x46546C67u);
    put_u32(out, 2u);
    put_u32(out, total_len);
    put_u32(out, json_len);
    put_u32(out, 0x4E4F534Au);
    out.write(json_str.data(), static_cast<std::streamsize>(json_str.size()));
    put_u32(out, bin_len);
    put_u32(out, 0x004E4942u);
    out.write(reinterpret_cast<const char*>(bin.data()),
              static_cast<std::streamsize>(bin.size()));
    if (!out.good()) {
        err = "terrain GLB write failed";
        return false;
    }
    return true;
}

template <class Fn>
void write_csv_numbers(std::ostream& os, size_t count, Fn fn)
{
    os << "a: ";
    for (size_t i = 0; i < count; ++i) {
        if (i) os << ",";
        os << fn(i);
    }
}

struct FbxBuf {
    std::vector<uint8_t> data;
    void u8(uint8_t v) { data.push_back(v); }
    void u32(uint32_t v)
    {
        u8(uint8_t(v & 0xffu));
        u8(uint8_t((v >> 8) & 0xffu));
        u8(uint8_t((v >> 16) & 0xffu));
        u8(uint8_t((v >> 24) & 0xffu));
    }
    void u64(uint64_t v)
    {
        u32(uint32_t(v & 0xffffffffu));
        u32(uint32_t((v >> 32) & 0xffffffffu));
    }
    void i32(int32_t v) { u32(uint32_t(v)); }
    void i64(int64_t v) { u64(uint64_t(v)); }
    void f64(double v)
    {
        uint64_t u = 0;
        std::memcpy(&u, &v, 8);
        u64(u);
    }
    void bytes(const void* ptr, size_t size)
    {
        const auto* p = static_cast<const uint8_t*>(ptr);
        data.insert(data.end(), p, p + size);
    }
};

struct FbxProp {
    enum Type { I32, I64, F64, STRING, ARR_I32, ARR_F64 } type;
    int32_t i32 = 0;
    int64_t i64 = 0;
    double f64 = 0.0;
    std::string str;
    std::vector<int32_t> ai;
    std::vector<double> ad;

    static FbxProp I(int32_t v)
    {
        FbxProp p;
        p.type = I32;
        p.i32 = v;
        return p;
    }
    static FbxProp L(int64_t v)
    {
        FbxProp p;
        p.type = I64;
        p.i64 = v;
        return p;
    }
    static FbxProp D(double v)
    {
        FbxProp p;
        p.type = F64;
        p.f64 = v;
        return p;
    }
    static FbxProp S(std::string v)
    {
        FbxProp p;
        p.type = STRING;
        p.str = std::move(v);
        return p;
    }
    static FbxProp AI(std::vector<int32_t> v)
    {
        FbxProp p;
        p.type = ARR_I32;
        p.ai = std::move(v);
        return p;
    }
    static FbxProp AD(std::vector<double> v)
    {
        FbxProp p;
        p.type = ARR_F64;
        p.ad = std::move(v);
        return p;
    }

    void emit(FbxBuf& out) const
    {
        switch (type) {
            case I32:
                out.u8('I');
                out.i32(i32);
                break;
            case I64:
                out.u8('L');
                out.i64(i64);
                break;
            case F64:
                out.u8('D');
                out.f64(f64);
                break;
            case STRING:
                out.u8('S');
                out.u32(uint32_t(str.size()));
                out.bytes(str.data(), str.size());
                break;
            case ARR_I32:
                out.u8('i');
                out.u32(uint32_t(ai.size()));
                out.u32(0);
                out.u32(uint32_t(ai.size() * sizeof(int32_t)));
                for (int32_t v : ai) out.i32(v);
                break;
            case ARR_F64:
                out.u8('d');
                out.u32(uint32_t(ad.size()));
                out.u32(0);
                out.u32(uint32_t(ad.size() * sizeof(double)));
                for (double v : ad) out.f64(v);
                break;
        }
    }
};

struct FbxNode {
    std::string name;
    std::vector<FbxProp> props;
    std::vector<FbxNode> children;

    explicit FbxNode(std::string n = {}) : name(std::move(n)) {}
    FbxNode& prop(FbxProp p)
    {
        props.push_back(std::move(p));
        return *this;
    }
    FbxNode& child(FbxNode n)
    {
        children.push_back(std::move(n));
        return children.back();
    }
};

void fbx_write_node(FbxBuf& out, const FbxNode& n)
{
    const size_t header = out.data.size();
    out.u32(0);
    out.u32(uint32_t(n.props.size()));
    const size_t plen_pos = out.data.size();
    out.u32(0);
    out.u8(uint8_t(n.name.size()));
    out.bytes(n.name.data(), n.name.size());
    const size_t props_start = out.data.size();
    for (const auto& p : n.props) p.emit(out);
    const uint32_t plen = uint32_t(out.data.size() - props_start);
    out.data[plen_pos + 0] = uint8_t(plen & 0xffu);
    out.data[plen_pos + 1] = uint8_t((plen >> 8) & 0xffu);
    out.data[plen_pos + 2] = uint8_t((plen >> 16) & 0xffu);
    out.data[plen_pos + 3] = uint8_t((plen >> 24) & 0xffu);
    for (const auto& c : n.children) fbx_write_node(out, c);
    if (!n.children.empty()) {
        for (int i = 0; i < 13; ++i) out.u8(0);
    }
    const uint32_t end = uint32_t(out.data.size());
    out.data[header + 0] = uint8_t(end & 0xffu);
    out.data[header + 1] = uint8_t((end >> 8) & 0xffu);
    out.data[header + 2] = uint8_t((end >> 16) & 0xffu);
    out.data[header + 3] = uint8_t((end >> 24) & 0xffu);
}

FbxNode fbx_p(const char* name,
              const char* type1,
              const char* type2,
              const char* flags,
              std::vector<FbxProp> values)
{
    FbxNode p("P");
    p.prop(FbxProp::S(name));
    p.prop(FbxProp::S(type1));
    p.prop(FbxProp::S(type2));
    p.prop(FbxProp::S(flags));
    for (auto& v : values) p.prop(std::move(v));
    return p;
}

std::string fbx_obj_name(const std::string& name, const char* klass)
{
    std::string out = name;
    out.push_back('\0');
    out.push_back('\1');
    out += klass;
    return out;
}

bool write_terrain_fbx(const TerrainMesh& mesh,
                       const std::filesystem::path& path,
                       std::string& err)
{
    if (!mesh.ok || mesh.positions.empty() || mesh.indices.empty()) {
        err = "terrain mesh is empty";
        return false;
    }
    const int64_t geom_id = 100001;
    const int64_t model_id = 100002;
    const int64_t mat_id = 100003;
    const size_t vcount = mesh.positions.size() / 3;
    const size_t icount = mesh.indices.size();
    std::vector<float> terrain_uv01;
    if (mesh.width > 1 && mesh.height > 1 &&
        vcount == size_t(mesh.width) * size_t(mesh.height)) {
        terrain_uv01.resize(vcount * 2);
        for (uint32_t y = 0; y < mesh.height; ++y) {
            for (uint32_t x = 0; x < mesh.width; ++x) {
                const size_t i = size_t(y) * mesh.width + x;
                terrain_uv01[i * 2 + 0] =
                    float(x) / float(mesh.width - 1);
                terrain_uv01[i * 2 + 1] =
                    float(y) / float(mesh.height - 1);
            }
        }
    }

    FbxNode root;
    {
        FbxNode hdr("FBXHeaderExtension");
        hdr.child(FbxNode("FBXHeaderVersion")).prop(FbxProp::I(1003));
        hdr.child(FbxNode("FBXVersion")).prop(FbxProp::I(7400));
        hdr.child(FbxNode("EncryptionType")).prop(FbxProp::I(0));
        hdr.child(FbxNode("Creator")).prop(FbxProp::S("Fable2AssetBrowser"));
        root.child(std::move(hdr));
    }
    root.child(FbxNode("CreationTime")).prop(FbxProp::S("1970-01-01 00:00:00:000"));
    root.child(FbxNode("Creator")).prop(FbxProp::S("Fable2AssetBrowser"));
    {
        FbxNode gs("GlobalSettings");
        gs.child(FbxNode("Version")).prop(FbxProp::I(1000));
        FbxNode p70("Properties70");
        p70.child(fbx_p("UpAxis", "int", "Integer", "", { FbxProp::I(2) }));
        p70.child(fbx_p("UpAxisSign", "int", "Integer", "", { FbxProp::I(1) }));
        p70.child(fbx_p("FrontAxis", "int", "Integer", "", { FbxProp::I(1) }));
        p70.child(fbx_p("FrontAxisSign", "int", "Integer", "", { FbxProp::I(-1) }));
        p70.child(fbx_p("CoordAxis", "int", "Integer", "", { FbxProp::I(0) }));
        p70.child(fbx_p("CoordAxisSign", "int", "Integer", "", { FbxProp::I(1) }));
        p70.child(fbx_p("UnitScaleFactor", "double", "Number", "", { FbxProp::D(100.0) }));
        p70.child(fbx_p("OriginalUnitScaleFactor", "double", "Number", "", { FbxProp::D(100.0) }));
        gs.child(std::move(p70));
        root.child(std::move(gs));
    }
    {
        FbxNode defs("Definitions");
        defs.child(FbxNode("Version")).prop(FbxProp::I(100));
        defs.child(FbxNode("Count")).prop(FbxProp::I(4));
        auto ot = [&](const char* name) {
            FbxNode n("ObjectType");
            n.prop(FbxProp::S(name));
            n.child(FbxNode("Count")).prop(FbxProp::I(1));
            defs.child(std::move(n));
        };
        ot("GlobalSettings");
        ot("Geometry");
        ot("Model");
        ot("Material");
        root.child(std::move(defs));
    }

    FbxNode objects("Objects");
    FbxNode geom("Geometry");
    geom.prop(FbxProp::L(geom_id));
    geom.prop(FbxProp::S(fbx_obj_name("Geometry::terrain", "Geometry")));
    geom.prop(FbxProp::S("Mesh"));
    geom.child(FbxNode("Vertices")).prop(FbxProp::AD(
        std::vector<double>(mesh.positions.begin(), mesh.positions.end())));

    std::vector<int32_t> pvi;
    pvi.reserve(icount);
    for (size_t i = 0; i + 2 < icount; i += 3) {
        pvi.push_back(int32_t(mesh.indices[i + 0]));
        pvi.push_back(int32_t(mesh.indices[i + 1]));
        pvi.push_back(~int32_t(mesh.indices[i + 2]));
    }
    geom.child(FbxNode("PolygonVertexIndex")).prop(FbxProp::AI(std::move(pvi)));
    geom.child(FbxNode("GeometryVersion")).prop(FbxProp::I(124));

    if (mesh.normals.size() / 3 == vcount) {
        FbxNode n("LayerElementNormal");
        n.prop(FbxProp::I(0));
        n.child(FbxNode("Version")).prop(FbxProp::I(101));
        n.child(FbxNode("Name")).prop(FbxProp::S(""));
        n.child(FbxNode("MappingInformationType")).prop(FbxProp::S("ByVertice"));
        n.child(FbxNode("ReferenceInformationType")).prop(FbxProp::S("Direct"));
        n.child(FbxNode("Normals")).prop(FbxProp::AD(
            std::vector<double>(mesh.normals.begin(), mesh.normals.end())));
        geom.child(std::move(n));
    }
    if (mesh.uvs.size() / 2 == vcount) {
        FbxNode uv("LayerElementUV");
        uv.prop(FbxProp::I(0));
        uv.child(FbxNode("Version")).prop(FbxProp::I(101));
        uv.child(FbxNode("Name")).prop(FbxProp::S("UVMap"));
        uv.child(FbxNode("MappingInformationType")).prop(FbxProp::S("ByVertice"));
        uv.child(FbxNode("ReferenceInformationType")).prop(FbxProp::S("Direct"));
        std::vector<double> vals(mesh.uvs.begin(), mesh.uvs.end());
        for (size_t i = 1; i < vals.size(); i += 2) {
            vals[i] = 1.0 - vals[i];
        }
        uv.child(FbxNode("UV")).prop(FbxProp::AD(std::move(vals)));
        geom.child(std::move(uv));
    }
    if (!terrain_uv01.empty()) {
        FbxNode uv("LayerElementUV");
        uv.prop(FbxProp::I(1));
        uv.child(FbxNode("Version")).prop(FbxProp::I(101));
        uv.child(FbxNode("Name")).prop(FbxProp::S("TerrainWeights"));
        uv.child(FbxNode("MappingInformationType")).prop(FbxProp::S("ByVertice"));
        uv.child(FbxNode("ReferenceInformationType")).prop(FbxProp::S("Direct"));
        std::vector<double> vals(terrain_uv01.begin(), terrain_uv01.end());
        for (size_t i = 1; i < vals.size(); i += 2) {
            vals[i] = 1.0 - vals[i];
        }
        uv.child(FbxNode("UV")).prop(FbxProp::AD(std::move(vals)));
        geom.child(std::move(uv));
    }
    {
        FbxNode lm("LayerElementMaterial");
        lm.prop(FbxProp::I(0));
        lm.child(FbxNode("Version")).prop(FbxProp::I(101));
        lm.child(FbxNode("Name")).prop(FbxProp::S(""));
        lm.child(FbxNode("MappingInformationType")).prop(FbxProp::S("AllSame"));
        lm.child(FbxNode("ReferenceInformationType")).prop(FbxProp::S("IndexToDirect"));
        lm.child(FbxNode("Materials")).prop(FbxProp::AI({ 0 }));
        geom.child(std::move(lm));
    }
    FbxNode layer("Layer");
    layer.prop(FbxProp::I(0));
    layer.child(FbxNode("Version")).prop(FbxProp::I(100));
    auto le = [&](const char* type, int idx) {
        FbxNode e("LayerElement");
        e.child(FbxNode("Type")).prop(FbxProp::S(type));
        e.child(FbxNode("TypedIndex")).prop(FbxProp::I(idx));
        layer.child(std::move(e));
    };
    if (mesh.normals.size() / 3 == vcount) {
        le("LayerElementNormal", 0);
    }
    if (mesh.uvs.size() / 2 == vcount) {
        le("LayerElementUV", 0);
    }
    if (!terrain_uv01.empty()) {
        le("LayerElementUV", 1);
    }
    le("LayerElementMaterial", 0);
    geom.child(std::move(layer));
    objects.child(std::move(geom));

    FbxNode model("Model");
    model.prop(FbxProp::L(model_id));
    model.prop(FbxProp::S(fbx_obj_name("Model::terrain", "Model")));
    model.prop(FbxProp::S("Mesh"));
    model.child(FbxNode("Version")).prop(FbxProp::I(232));
    FbxNode mp("Properties70");
    mp.child(fbx_p("Lcl Translation", "Lcl Translation", "", "A",
                   { FbxProp::D(0.0), FbxProp::D(0.0), FbxProp::D(0.0) }));
    mp.child(fbx_p("Lcl Rotation", "Lcl Rotation", "", "A",
                   { FbxProp::D(0.0), FbxProp::D(0.0), FbxProp::D(0.0) }));
    mp.child(fbx_p("Lcl Scaling", "Lcl Scaling", "", "A",
                   { FbxProp::D(1.0), FbxProp::D(1.0), FbxProp::D(1.0) }));
    model.child(std::move(mp));
    objects.child(std::move(model));

    FbxNode mat("Material");
    mat.prop(FbxProp::L(mat_id));
    mat.prop(FbxProp::S(fbx_obj_name(
        "Material::Fable terrain shader data in .fable", "Material")));
    mat.prop(FbxProp::S(""));
    mat.child(FbxNode("Version")).prop(FbxProp::I(102));
    mat.child(FbxNode("ShadingModel")).prop(FbxProp::S("phong"));
    mat.child(FbxNode("MultiLayer")).prop(FbxProp::I(0));
    objects.child(std::move(mat));
    root.child(std::move(objects));

    FbxNode conns("Connections");
    auto conn = [&](std::vector<FbxProp> props) {
        FbxNode c("C");
        for (auto& p : props) c.prop(std::move(p));
        conns.child(std::move(c));
    };
    conn({ FbxProp::S("OO"), FbxProp::L(geom_id), FbxProp::L(model_id) });
    conn({ FbxProp::S("OO"), FbxProp::L(model_id), FbxProp::L(0) });
    conn({ FbxProp::S("OO"), FbxProp::L(mat_id), FbxProp::L(model_id) });
    root.child(std::move(conns));
    root.child(FbxNode("Takes"));

    FbxBuf out;
    static const uint8_t magic[] = {
        'K','a','y','d','a','r','a',' ','F','B','X',' ','B','i','n','a',
        'r','y',' ',' ', 0x00, 0x1a, 0x00
    };
    out.bytes(magic, sizeof(magic));
    out.u32(7400);
    for (const auto& child : root.children) fbx_write_node(out, child);
    for (int i = 0; i < 13; ++i) out.u8(0);
    for (int i = 0; i < 16; ++i) out.u8(0);
    while (out.data.size() & 0xfu) out.u8(0);
    out.u32(0);
    out.u32(7400);
    for (int i = 0; i < 120; ++i) out.u8(0);
    static const uint8_t footer[] = {
        0xf8, 0x5a, 0x8c, 0x6a, 0xde, 0xf5, 0xd9, 0x7e,
        0xec, 0xe9, 0x0c, 0xe3, 0x75, 0x8f, 0x29, 0x0b
    };
    out.bytes(footer, sizeof(footer));

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        err = ec.message();
        return false;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        err = "cannot open " + path.string();
        return false;
    }
    f.write(reinterpret_cast<const char*>(out.data.data()),
            static_cast<std::streamsize>(out.data.size()));
    if (!f.good()) {
        err = "terrain FBX write failed";
        return false;
    }
    return true;
}

struct TerrainWeightExport {
    int width = 0;
    int height = 0;
    int material_count = 0;
    std::vector<std::vector<uint8_t>> rgba;
};

bool build_terrain_weight_maps(const EhfParsedBody& parsed,
                               TerrainWeightExport& out)
{
    out = {};
    const bool has_pf99 =
        !parsed.splat_indices.empty() &&
        parsed.splat_w > 0 && parsed.splat_h > 0 &&
        parsed.splat_indices.size() ==
            size_t(parsed.splat_w) * size_t(parsed.splat_h);
    if (!has_pf99 || parsed.chunk_w == 0 || parsed.chunk_h == 0 ||
        parsed.lods.empty()) {
        return false;
    }

    const int CW = int(parsed.chunk_w);
    const int CH = int(parsed.chunk_h);
    const int N =
        std::min<int>(std::max<int>(1, int(parsed.lods.size())), 32);
    const int W = CW * 32 + 1;
    const int H = CH * 32 + 1;
    const size_t area = size_t(W) * size_t(H);
    std::vector<float> weights(size_t(N) * area, 0.0f);

    const auto mask_sample = [&](const EhfChunkLayer& layer,
                                 int px,
                                 int py) -> float
    {
        const std::vector<uint8_t>* map = &parsed.splat_indices;
        int mw = int(parsed.splat_w);
        int mh = int(parsed.splat_h);
        if (size_t(layer.name_idx) < parsed.paint_resources.size()) {
            const auto& pr = parsed.paint_resources[size_t(layer.name_idx)];
            if (!pr.data.empty() && pr.width > 0 && pr.height > 0) {
                map = &pr.data;
                mw  = int(pr.width);
                mh  = int(pr.height);
            }
        }
        if (map->empty() || mw <= 0 || mh <= 0) return 1.0f;
        const int ox = std::clamp(
            int(std::floor(layer.tile_uv[0] * float(mw))), 0, mw - 1);
        const int oy = std::clamp(
            int(std::floor(layer.tile_uv[1] * float(mh))), 0, mh - 1);
        const int sx = std::clamp(ox + px, 0, mw - 1);
        const int sy = std::clamp(oy + py, 0, mh - 1);
        return float((*map)[size_t(sy) * size_t(mw) + size_t(sx)]) / 255.0f;
    };

    const size_t expected_chunks = size_t(CW) * size_t(CH);
    for (size_t ci = 0;
         ci < parsed.chunks.size() && ci < expected_chunks;
         ++ci) {
        const int cx = int(ci / size_t(CH));
        const int cy = int(ci % size_t(CH));
        const auto& chunk = parsed.chunks[ci];
        const int layer_count =
            std::min<int>(int(chunk.layers.size()), 16);

        for (int li = 0; li < layer_count; ++li) {
            const auto& layer = chunk.layers[size_t(li)];
            const int mat =
                std::clamp<int>(int(layer.material_idx), 0, N - 1);
            for (int py = 0; py <= 32; ++py) {
                for (int px = 0; px <= 32; ++px) {
                    const float layer_w = mask_sample(layer, px, py);
                    if (layer_w <= 0.0001f) continue;

                    const int gx = cx * 32 + px;
                    const int gy = cy * 32 + py;
                    if (gx < 0 || gy < 0 || gx >= W || gy >= H) continue;
                    weights[size_t(mat) * area +
                            size_t(gy) * size_t(W) + size_t(gx)] += layer_w;
                }
            }
        }
    }

    for (size_t p = 0; p < area; ++p) {
        float sum = 0.0f;
        for (int m = 0; m < N; ++m) {
            sum += weights[size_t(m) * area + p];
        }
        if (sum > 0.00001f) {
            const float inv = 1.0f / sum;
            for (int m = 0; m < N; ++m) {
                weights[size_t(m) * area + p] *= inv;
            }
        } else {
            weights[p] = 1.0f;
        }
    }

    out.width = W;
    out.height = H;
    out.material_count = N;
    out.rgba.resize(size_t(N));
    for (int m = 0; m < N; ++m) {
        auto& img = out.rgba[size_t(m)];
        img.resize(area * 4);
        for (size_t p = 0; p < area; ++p) {
            const float w =
                std::clamp(weights[size_t(m) * area + p], 0.0f, 1.0f);
            const uint8_t b = uint8_t(std::lround(w * 255.0f));
            img[p * 4 + 0] = b;
            img[p * 4 + 1] = b;
            img[p * 4 + 2] = b;
            img[p * 4 + 3] = b;
        }
    }
    return true;
}

std::array<float, 16> instance_matrix(const PropInstance& inst)
{
    const float px = inst.values[0];
    const float py = inst.values[2];
    const float pz = inst.values[1];
    std::array<float, 16> m{};
    if (inst.has_full_transform) {
        float scale = inst.values[12];
        if (!std::isfinite(scale) || scale == 0.0f) scale = 1.0f;
        const float* r = &inst.values[3];
        m = {
            r[0] * scale, r[1] * scale, r[2] * scale, px,
            r[3] * scale, r[4] * scale, r[5] * scale, py,
            r[6] * scale, r[7] * scale, r[8] * scale, pz,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        return m;
    }

    const float s = inst.values[6];
    const float c = inst.values[7];
    const float sx = inst.values[9]  == 0.0f ? 1.0f : inst.values[9];
    const float sy = inst.values[10] == 0.0f ? sx : inst.values[10];
    const float sz = inst.values[11] == 0.0f ? sx : inst.values[11];
    m = {
        c * sx, 0.0f, s * sy, px,
        0.0f, sz, 0.0f, py,
        -s * sx, 0.0f, c * sy, pz,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    return m;
}

std::string model_output_name(const std::string& source,
                              const char* ext)
{
    return sanitize_name(stem_of(source)) + "_" + hex8(fnv1a64(source)) + ext;
}

const char* terrain_texture_extension(MdlTexExport::Format fmt)
{
    switch (fmt) {
        case MdlTexExport::Format::DDS: return ".dds";
        case MdlTexExport::Format::PNG: return ".png";
        case MdlTexExport::Format::JPG: return ".jpg";
    }
    return ".dds";
}

bool export_one_terrain_texture(const std::string& tex_name,
                                const std::string& preferred_bnk,
                                const std::filesystem::path& texture_dir,
                                const std::string& texture_rel_root,
                                std::unordered_map<std::string, std::string>& cache,
                                std::string& out_rel)
{
    out_rel.clear();
    if (tex_name.empty()) return false;
    const std::string key = lower_copy(to_slash(tex_name));
    auto hit = cache.find(key);
    if (hit != cache.end()) {
        out_rel = hit->second;
        return true;
    }

    const auto fmt = MdlTexExport::Format::PNG;
    const std::string leaf =
        sanitize_name(stem_of(tex_name)) + "_" + hex8(fnv1a64(key)) +
        terrain_texture_extension(fmt);
    const auto path = texture_dir / leaf;
    out_rel = texture_rel_root + "/" + leaf;
    if (file_exists_nonempty(path)) {
        cache[key] = out_rel;
        return true;
    }

    std::vector<unsigned char> tex_buf;
    if (!build_any_tex_buffer_for_name(tex_name, tex_buf, preferred_bnk)) {
        return false;
    }
    MdlTexExport::EncodedTex enc;
    if (!MdlTexExport::encode_largest_mip(tex_buf, fmt, enc) ||
        enc.bytes.empty()) {
        return false;
    }

    std::string err;
    if (!write_bytes(path, enc.bytes, err)) {
        OutputLog::warn("level export: terrain texture write failed: " + err);
        return false;
    }
    cache[key] = out_rel;
    return true;
}

void write_matrix_json(std::ostream& os, const std::array<float, 16>& m)
{
    os << "[";
    for (size_t i = 0; i < m.size(); ++i) {
        if (i) os << ",";
        os << m[i];
    }
    os << "]";
}

bool run_export(const FlatAssetEntry& entry, ExportFormat format)
{
    const char* ext = (format == ExportFormat::GLB) ? ".glb" : ".fbx";
    const char* fmt_label = (format == ExportFormat::GLB) ? "GLB" : "FBX";

    const std::filesystem::path root =
        S.export_dir.empty() ? std::filesystem::path("extracted")
                             : std::filesystem::path(S.export_dir);
    const std::string folder_name = level_folder_name(entry);
    const auto out_dir = root / folder_name;
    const auto model_dir = out_dir / "models";
    const auto terrain_dir = out_dir / "terrain";
    const auto terrain_texture_dir = terrain_dir / "textures";

    std::error_code ec;
    std::filesystem::create_directories(model_dir, ec);
    if (ec) {
        OutputLog::error("level export: cannot create " + model_dir.string() +
                         " (" + ec.message() + ")");
        return false;
    }
    std::filesystem::create_directories(terrain_texture_dir, ec);
    if (ec) {
        OutputLog::error("level export: cannot create " +
                         terrain_texture_dir.string() + " (" +
                         ec.message() + ")");
        return false;
    }

    ExportOnlyLevelLoadGuard export_load_guard;

    progress_update(3, 100, "Preparing level export...");
    if (!Open(entry) || S.cancel_requested.load() || S.exiting.load()) {
        return false;
    }

    progress_update(18, 100, "Collecting export data...");

    const TerrainMesh terrain_mesh = g_pending_terrain_mesh;
    const std::vector<uint8_t> terrain_ehf = g_pending_terrain_ehf_bytes;
    const std::vector<PropBlock> prop_blocks = g_pending_level_prop_blocks;
    const std::string model_body_bnk = g_pending_level_model_body_bnk;
    const FlatAssetEntry terrain_entry = g_pending_terrain_level_entry;
    const int ghf_w = g_pending_terrain_ghf_width;
    const int ghf_h = g_pending_terrain_ghf_height;
    const float ghf_tile = g_pending_terrain_ghf_tile_size;

    g_pending_terrain_load.store(false);

    std::map<std::string, size_t> instance_counts;
    for (const auto& block : prop_blocks) {
        if (block.model_path.empty()) continue;
        instance_counts[block.model_path] += block.instances.size();
    }

    std::unordered_map<std::string, std::string> model_files;
    int exported_models = 0;
    int reused_models = 0;
    int failed_models = 0;
    int model_index = 0;
    const int model_total =
        std::max(1, static_cast<int>(instance_counts.size()));

    for (const auto& kv : instance_counts) {
        if (S.cancel_requested.load() || S.exiting.load()) return false;
        const std::string& model_path = kv.first;
        const std::string out_leaf = model_output_name(model_path, ext);
        const auto out_path = model_dir / out_leaf;
        std::vector<unsigned char> mdl_buf;
        progress_update(70 + (model_index * 12) / model_total, 100,
                        "Exporting model " + std::to_string(model_index + 1) +
                        "/" + std::to_string(model_total));
        if (file_exists_nonempty(out_path)) {
            model_files[model_path] = std::string("models/") + out_leaf;
            ++reused_models;
            ++model_index;
            continue;
        }
        if (!build_mdl_buffer_for_name_with_body(model_path, model_body_bnk,
                                                 mdl_buf)) {
            ++failed_models;
            ++model_index;
            continue;
        }

        std::string err;
        bool ok = false;
        try {
            if (format == ExportFormat::GLB) {
                ok = mdl_to_glb_full(mdl_buf, out_path.string(),
                                     model_path, err);
            } else {
                ok = mdl_to_fbx_full(mdl_buf, out_path.string(),
                                     model_path, err, false);
            }
        } catch (const std::exception& ex) {
            err = ex.what();
        } catch (...) {
            err = "unknown exporter exception";
        }
        if (ok) {
            model_files[model_path] = std::string("models/") + out_leaf;
            ++exported_models;
        } else {
            ++failed_models;
            if (!err.empty()) {
                OutputLog::warn("level export: model failed: " + model_path +
                                " (" + err + ")");
            }
        }
        ++model_index;
    }

    if (S.cancel_requested.load() || S.exiting.load()) return false;

    EhfParsedBody ehf;
    const bool ehf_ok = ParseEhfBody(terrain_ehf, ehf);
    if (!ehf_ok && !terrain_ehf.empty()) {
        OutputLog::warn("level export: terrain material parse failed: " +
                        ehf.error);
    }

    progress_update(84, 100, "Exporting terrain...");
    const std::string terrain_file =
        std::string("terrain/terrain") +
        ((format == ExportFormat::GLB) ? ".glb" : ".fbx");
    const auto terrain_path = out_dir / terrain_file;
    std::string terrain_err;
    const bool terrain_ok =
        (format == ExportFormat::GLB)
            ? write_terrain_glb(terrain_mesh, terrain_path, terrain_err)
            : write_terrain_fbx(terrain_mesh, terrain_path, terrain_err);
    if (!terrain_ok) {
        OutputLog::warn("level export: terrain mesh failed: " + terrain_err);
    }

    std::unordered_map<std::string, std::string> terrain_tex_cache;
    std::vector<std::array<std::string, 6>> lod_texture_files;
    std::vector<std::string> weight_files;
    int weight_w = 0;
    int weight_h = 0;
    if (ehf_ok) {
        lod_texture_files.resize(ehf.lods.size());
        for (size_t li = 0; li < ehf.lods.size(); ++li) {
            for (int si = 0; si < 6; ++si) {
                std::string rel;
                if (export_one_terrain_texture(
                        ehf.lods[li].strs[si],
                        terrain_entry.bnk_path.empty()
                            ? model_body_bnk
                            : terrain_entry.bnk_path,
                        terrain_texture_dir, "terrain/textures",
                        terrain_tex_cache, rel)) {
                    lod_texture_files[li][si] = rel;
                }
            }
        }

        TerrainWeightExport weights;
        const auto weight_dir = terrain_dir / "weights";
        std::filesystem::create_directories(weight_dir, ec);
        const int expected_weights =
            std::min<int>(std::max<int>(1, int(ehf.lods.size())), 32);
        weight_w = int(ehf.chunk_w) * 32 + 1;
        weight_h = int(ehf.chunk_h) * 32 + 1;
        weight_files.resize(size_t(expected_weights));
        bool weights_reused = expected_weights > 0;
        for (int wi = 0; wi < expected_weights; ++wi) {
            const std::string leaf =
                "weight_" + (wi < 10 ? std::string("0") : std::string()) +
                std::to_string(wi) + ".png";
            const auto path = weight_dir / leaf;
            weight_files[size_t(wi)] = std::string("terrain/weights/") + leaf;
            if (!file_exists_nonempty(path)) {
                weights_reused = false;
            }
        }
        if (!weights_reused && build_terrain_weight_maps(ehf, weights)) {
            weight_w = weights.width;
            weight_h = weights.height;
            weight_files.resize(size_t(weights.material_count));
            for (int wi = 0; wi < weights.material_count; ++wi) {
                const std::string leaf =
                    "weight_" + (wi < 10 ? std::string("0") : std::string()) +
                    std::to_string(wi) + ".png";
                const auto path = weight_dir / leaf;
                if (tex_export_png(path.string(),
                                   weights.rgba[size_t(wi)].data(),
                                   weights.width,
                                   weights.height)) {
                    weight_files[size_t(wi)] =
                        std::string("terrain/weights/") + leaf;
                }
            }
        } else if (!weights_reused) {
            OutputLog::warn("level export: terrain weight maps unavailable");
        }
    }

    std::string splat_rel;
    if (ehf_ok && !ehf.splat_indices.empty()) {
        splat_rel = "terrain/splat_indices.bin";
        std::string err;
        const auto splat_path = out_dir / splat_rel;
        if (!file_exists_nonempty(splat_path) &&
            !write_bytes(splat_path, ehf.splat_indices, err)) {
            OutputLog::warn("level export: splat write failed: " + err);
            splat_rel.clear();
        }
    }

    progress_update(93, 100, "Writing .fable...");
    std::ostringstream js;
    js << std::setprecision(9);
    js << "{\n";
    js << "  \"version\": 1,\n";
    js << "  \"format\": \"" << fmt_label << "\",\n";
    js << "  \"level\": {\n";
    js << "    \"name\": \"" << json_escape(folder_name) << "\",\n";
    js << "    \"source\": \"" << json_escape(to_slash(entry.full_path)) << "\"\n";
    js << "  },\n";
    js << "  \"models\": [\n";
    bool first_model = true;
    for (const auto& kv : model_files) {
        if (!first_model) js << ",\n";
        first_model = false;
        js << "    {\"source\":\"" << json_escape(to_slash(kv.first))
           << "\",\"file\":\"" << json_escape(kv.second)
           << "\",\"instances\":" << instance_counts[kv.first] << "}";
    }
    js << "\n  ],\n";
    js << "  \"instances\": [\n";
    bool first_inst = true;
    size_t instance_written = 0;
    for (const auto& block : prop_blocks) {
        auto mf = model_files.find(block.model_path);
        if (mf == model_files.end()) continue;
        for (size_t ii = 0; ii < block.instances.size(); ++ii) {
            const auto& inst = block.instances[ii];
            const auto mat = instance_matrix(inst);
            if (!first_inst) js << ",\n";
            first_inst = false;
            js << "    {\"model\":\"" << json_escape(mf->second)
               << "\",\"source\":\"" << json_escape(to_slash(block.model_path))
               << "\",\"type\":" << block.type
               << ",\"hash\":" << inst.hash
               << ",\"position\":[" << inst.values[0] << ","
               << inst.values[2] << "," << inst.values[1] << "]"
               << ",\"matrix\":";
            write_matrix_json(js, mat);
            js << ",\"fullTransform\":"
               << (inst.has_full_transform ? "true" : "false")
               << ",\"flags\":[" << int(inst.flags[0]) << ","
               << int(inst.flags[1]) << "," << int(inst.flags[2])
               << "],\"raw\":[";
            for (int vi = 0; vi < 20; ++vi) {
                if (vi) js << ",";
                js << inst.values[vi];
            }
            js << "]}";
            ++instance_written;
        }
    }
    js << "\n  ],\n";
    js << "  \"terrain\": {\n";
    js << "    \"mesh\": \"" << json_escape(terrain_file) << "\",\n";
    js << "    \"exported\": " << (terrain_ok ? "true" : "false") << ",\n";
    const std::string terrain_source =
        terrain_entry.full_path.empty() ? g_pending_terrain_label
                                        : terrain_entry.full_path;
    js << "    \"sourceEhf\": \"" << json_escape(to_slash(terrain_source)) << "\",\n";
    js << "    \"width\": " << ghf_w << ",\n";
    js << "    \"height\": " << ghf_h << ",\n";
    js << "    \"tileSize\": " << ghf_tile << ",\n";
    js << "    \"minHeight\": " << terrain_mesh.min_height << ",\n";
    js << "    \"maxHeight\": " << terrain_mesh.max_height << ",\n";
    js << "    \"materialsParsed\": " << (ehf_ok ? "true" : "false");
    if (ehf_ok) {
        js << ",\n    \"splat\": {\"file\":\"" << json_escape(splat_rel)
           << "\",\"width\":" << ehf.splat_w
           << ",\"height\":" << ehf.splat_h << "},\n";
        js << "    \"weightMaps\": {\"width\":" << weight_w
           << ",\"height\":" << weight_h << ",\"files\":[";
        for (size_t wi = 0; wi < weight_files.size(); ++wi) {
            if (wi) js << ",";
            js << "\"" << json_escape(weight_files[wi]) << "\"";
        }
        js << "]},\n";
        js << "    \"uvSets\": {\"tiled\":0,\"weights\":1},\n";
        js << "    \"paintResources\": [";
        for (size_t i = 0; i < ehf.paint_resources.size(); ++i) {
            if (i) js << ",";
            const auto& pr = ehf.paint_resources[i];
            js << "{\"width\":" << pr.width << ",\"height\":" << pr.height
               << ",\"pixelFormat\":" << pr.pixel_format << "}";
        }
        js << "],\n";
        js << "    \"lods\": [\n";
        for (size_t li = 0; li < ehf.lods.size(); ++li) {
            if (li) js << ",\n";
            const auto& lod = ehf.lods[li];
            js << "      {\"index\":" << li
               << ",\"materialFlags\":" << lod.material_flags
               << ",\"strings\":[";
            for (int si = 0; si < 6; ++si) {
                if (si) js << ",";
                js << "\"" << json_escape(to_slash(lod.strs[si])) << "\"";
            }
            js << "],\"files\":[";
            for (int si = 0; si < 6; ++si) {
                if (si) js << ",";
                js << "\"" << json_escape(lod_texture_files[li][si]) << "\"";
            }
            js << "],\"weight\":\"";
            if (li < weight_files.size()) {
                js << json_escape(weight_files[li]);
            }
            js << "\",\"params\":[["
               << lod.params[0][0] << "," << lod.params[0][1] << ","
               << lod.params[0][2] << "],["
               << lod.params[1][0] << "," << lod.params[1][1] << ","
               << lod.params[1][2] << "]]}";
        }
        js << "\n    ],\n";
        js << "    \"chunks\": [";
        for (size_t ci = 0; ci < ehf.chunks.size(); ++ci) {
            if (ci) js << ",";
            const auto& ch = ehf.chunks[ci];
            js << "{\"origin\":[" << ch.origin[0] << "," << ch.origin[1]
               << "," << ch.origin[2] << "],\"extent\":["
               << ch.extent[0] << "," << ch.extent[1] << ","
               << ch.extent[2] << "],\"layers\":[";
            for (size_t li = 0; li < ch.layers.size(); ++li) {
                if (li) js << ",";
                const auto& layer = ch.layers[li];
                js << "{\"material\":" << layer.material_idx
                   << ",\"name\":" << layer.name_idx
                   << ",\"tileUv\":[" << layer.tile_uv[0] << ","
                   << layer.tile_uv[1] << "],\"maskScale\":["
                   << layer.mask_scale[0] << "," << layer.mask_scale[1]
                   << "],\"textureIdx\":["
                   << int(layer.texture_idx[0]) << ","
                   << int(layer.texture_idx[1]) << ","
                   << int(layer.texture_idx[2]) << ","
                   << int(layer.texture_idx[3]) << "],\"blend\":["
                   << int(layer.blend[0]) << "," << int(layer.blend[1])
                   << "," << int(layer.blend[2]) << ","
                   << int(layer.blend[3]) << "]}";
            }
            js << "]}";
        }
        js << "]\n";
    } else {
        js << "\n";
    }
    js << "  }\n";
    js << "}\n";

    const auto fable_path = out_dir / (folder_name + ".fable");
    std::string json_err;
    if (!write_text(fable_path, js.str(), json_err)) {
        OutputLog::error("level export: .fable write failed: " + json_err);
        return false;
    }

    progress_update(100, 100, "Done");
    std::ostringstream msg;
    msg << "Level exported: " << folder_name << " (" << model_files.size()
        << " model(s), " << instance_written << " placement(s)";
    if (reused_models > 0) msg << ", " << reused_models << " reused";
    if (exported_models > 0) msg << ", " << exported_models << " written";
    if (failed_models > 0) msg << ", " << failed_models << " model miss(es)";
    msg << ") -> " << out_dir.string();
    OutputLog::success(msg.str());
    return true;
}

}

bool IsExportInProgress()
{
    return g_level_exporting.load();
}

void ExportAsync(const FlatAssetEntry& entry, ExportFormat format)
{
    bool expected = false;
    if (!g_level_exporting.compare_exchange_strong(expected, true)) {
        OutputLog::warn("level export already in progress");
        return;
    }
    S.cancel_requested.store(false);
    progress_open(100, "Exporting level...");
    std::thread([entry, format]() {
        struct Guard {
            ~Guard()
            {
                progress_done();
                S.cancel_requested.store(false);
                g_level_exporting.store(false);
            }
        } guard;

        bool ok = false;
        try {
            ok = run_export(entry, format);
        } catch (const std::exception& ex) {
            OutputLog::error("level export failed: " + std::string(ex.what()));
        } catch (...) {
            OutputLog::error("level export failed: unknown exception");
        }

        if (S.cancel_requested.load()) {
            OutputLog::warn("Level export cancelled.");
        }
    }).detach();
}

}
