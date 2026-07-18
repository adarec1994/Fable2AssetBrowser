static void bind_generated_terrain_textures(
    ID3D11Device* device,
    const std::vector<GeneratedTerrainTexture>& textures,
    const char* log_prefix)
{
    if (!device || textures.empty() || g_mp.meshes.empty()) return;

    for (const GeneratedTerrainTexture& t : textures) {
        if (t.rgba.empty() || t.width <= 0 || t.height <= 0) continue;
        if (t.mesh_index >= g_mp.meshes.size()) continue;

        ID3D11ShaderResourceView* srv =
            t.mipped
                ? create_srv_from_rgba_mipped(device, t.width, t.height, t.rgba)
                : create_srv_from_rgba(device, t.width, t.height, t.rgba);
        if (!srv) continue;

        MPPerMesh& m = g_mp.meshes[t.mesh_index];
        if (m.srv_diffuse) m.srv_diffuse->Release();
        m.srv_diffuse      = srv;
        m.diffuse_visible  = true;
        m.diffuse_tex_name = t.label;
        const bool splat_active =
            (t.mesh_index == 0 && TerrainSplat::Get().ok);
        if (t.mesh_index == 0) {
            m.is_terrain = true;
            if (splat_active) {
                m.diffuse_tex_name = "ehf_splat_terrain";
            }
        }

        TerrainTextureRegistry::Register(t.label, t.rgba, t.width, t.height);
        if (splat_active) {
            OutputLog::success(
                "terrain SPLAT shader retained after prop upload "
                "(composite kept only as fallback)");
        } else {
            OutputLog::success(std::string(log_prefix) + ": " + t.label +
                               " (" + std::to_string(t.width) + "x" +
                               std::to_string(t.height) + ")");
        }
    }
}
