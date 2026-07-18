static void mp_release_mesh(MPPerMesh& m){
    if(m.vb){ m.vb->Release(); m.vb=nullptr; }
    if(m.ib){ m.ib->Release(); m.ib=nullptr; }
    if(m.srv_diffuse){ m.srv_diffuse->Release(); m.srv_diffuse=nullptr; }
    if(m.srv_normal){ m.srv_normal->Release(); m.srv_normal=nullptr; }
    if(m.srv_specular){ m.srv_specular->Release(); m.srv_specular=nullptr; }
    if(m.srv_metallic){ m.srv_metallic->Release(); m.srv_metallic=nullptr; }
    if(m.srv_extra){ m.srv_extra->Release(); m.srv_extra=nullptr; }
    m.index_count = 0;
}
static void mp_release(ModelPreview& mp){
    for(auto& m: mp.meshes) mp_release_mesh(m);
    mp.meshes.clear();
    Skybox::ReleaseD3D11Resources(mp);
    if(mp.vs){ mp.vs->Release(); mp.vs=nullptr; }
    if(mp.ps){ mp.ps->Release(); mp.ps=nullptr; }
    if(mp.vs_terrain){ mp.vs_terrain->Release(); mp.vs_terrain=nullptr; }
    if(mp.ps_terrain){ mp.ps_terrain->Release(); mp.ps_terrain=nullptr; }
    if(mp.ps_terrain_direct){ mp.ps_terrain_direct->Release(); mp.ps_terrain_direct=nullptr; }
    if(mp.ps_terrain_paint){ mp.ps_terrain_paint->Release(); mp.ps_terrain_paint=nullptr; }
    if(mp.cbuffer_terrain){ mp.cbuffer_terrain->Release(); mp.cbuffer_terrain=nullptr; }
    if(mp.cbuffer_terrain_paint){ mp.cbuffer_terrain_paint->Release(); mp.cbuffer_terrain_paint=nullptr; }
    if(mp.vs_water){ mp.vs_water->Release(); mp.vs_water=nullptr; }
    if(mp.ps_water){ mp.ps_water->Release(); mp.ps_water=nullptr; }
    if(mp.cbuffer_water){ mp.cbuffer_water->Release(); mp.cbuffer_water=nullptr; }
    if(mp.vs_weather){ mp.vs_weather->Release(); mp.vs_weather=nullptr; }
    if(mp.ps_weather){ mp.ps_weather->Release(); mp.ps_weather=nullptr; }
    if(mp.cbuffer_weather){ mp.cbuffer_weather->Release(); mp.cbuffer_weather=nullptr; }
    if(mp.cbuffer_fog){ mp.cbuffer_fog->Release(); mp.cbuffer_fog=nullptr; }
    if(mp.sampler_point){ mp.sampler_point->Release(); mp.sampler_point=nullptr; }
    if(mp.layout){ mp.layout->Release(); mp.layout=nullptr; }
    if(mp.cbuffer){ mp.cbuffer->Release(); mp.cbuffer=nullptr; }
    if(mp.bone_cb){ mp.bone_cb->Release(); mp.bone_cb=nullptr; }
    if(mp.sampler){ mp.sampler->Release(); mp.sampler=nullptr; }
    if(mp.rs){ mp.rs->Release(); mp.rs=nullptr; }
    if(mp.rs_wire){ mp.rs_wire->Release(); mp.rs_wire=nullptr; }
    if(mp.bs){ mp.bs->Release(); mp.bs=nullptr; }
    if(mp.bsAlpha){ mp.bsAlpha->Release(); mp.bsAlpha=nullptr; }
    if(mp.rtv){ mp.rtv->Release(); mp.rtv=nullptr; }
    if(mp.srv){ mp.srv->Release(); mp.srv=nullptr; }
    if(mp.color){ mp.color->Release(); mp.color=nullptr; }
    if(mp.dsv){ mp.dsv->Release(); mp.dsv=nullptr; }
    if(mp.depth){ mp.depth->Release(); mp.depth=nullptr; }
    if(mp.default_srv){ mp.default_srv->Release(); mp.default_srv=nullptr; }
    if(mp.dssWrite){ mp.dssWrite->Release(); mp.dssWrite=nullptr; }
    if(mp.dssNoWrite){ mp.dssNoWrite->Release(); mp.dssNoWrite=nullptr; }
    if(mp.dssNoWriteLEqual){ mp.dssNoWriteLEqual->Release(); mp.dssNoWriteLEqual=nullptr; }
    if(mp.dssWaterOnce){ mp.dssWaterOnce->Release(); mp.dssWaterOnce=nullptr; }
    for(auto& kv : mp.fx_tex_srv){ if(kv.second) kv.second->Release(); }
    mp.fx_tex_srv.clear();
    if(mp.vs_fx){ mp.vs_fx->Release(); mp.vs_fx=nullptr; }
    if(mp.ps_fx){ mp.ps_fx->Release(); mp.ps_fx=nullptr; }
    if(mp.layout_fx){ mp.layout_fx->Release(); mp.layout_fx=nullptr; }
    if(mp.cbuffer_fx){ mp.cbuffer_fx->Release(); mp.cbuffer_fx=nullptr; }
    if(mp.fx_vb){ mp.fx_vb->Release(); mp.fx_vb=nullptr; }
    mp.fx_vb_capacity = 0;
    if(mp.bs_fx_alpha){ mp.bs_fx_alpha->Release(); mp.bs_fx_alpha=nullptr; }
    if(mp.bs_fx_add){ mp.bs_fx_add->Release(); mp.bs_fx_add=nullptr; }
    for (auto& kv : mp.fx_blend_states)
        if (kv.second) kv.second->Release();
    mp.fx_blend_states.clear();
    if(mp.bs_water){ mp.bs_water->Release(); mp.bs_water=nullptr; }
    mp.fx_system.clear();
    mp.has_model = false;
}
