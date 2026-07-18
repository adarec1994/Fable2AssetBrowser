void MP_Render(ModelPreview& mp, const FlyCam& cam) {
    if (!mp.has_model) return;
    glBindFramebuffer(GL_FRAMEBUFFER, mp.fbo);
    glViewport(0, 0, mp.width, mp.height);
    glClearColor(0.22f, 0.22f, 0.22f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDisable(GL_CULL_FACE);
    glUseProgram(mp.shader_program);
    float cy = cosf(cam.yaw);
    float sy = sinf(cam.yaw);
    float cp = cosf(cam.pitch);
    float sp = sinf(cam.pitch);
    float forward[3] = { sy * cp, sp, cy * cp };
    float atx = cam.pos[0] + forward[0];
    float aty = cam.pos[1] + forward[1];
    float atz = cam.pos[2] + forward[2];
    float V[16], P[16], W[16], Tm[16], R[16], Tp[16], tmp[16];
    mat4_lookat(V, cam.pos[0], cam.pos[1], cam.pos[2], atx, aty, atz, 0, 1, 0);
    float fov = 60.0f * 3.14159265f / 180.0f, aspect = (float)mp.width / (float)mp.height;
    float far_plane = mp.radius * 100.0f;
    mat4_perspective(P, fov, aspect, 0.05f, far_plane);
    mat4_identity(W);
    if (!mp.no_tilt) {
        const float tiltX = -3.14159265f / 2.0f;
        mat4_translate(Tm, -mp.center[0], -mp.center[1], -mp.center[2]);
        mat4_rotateX(R, tiltX);
        mat4_translate(Tp, mp.center[0], mp.center[1], mp.center[2]);
        mat4_mult(tmp, R, Tm); mat4_mult(W, Tp, tmp);
    }
    float MV[16], MVP[16];
    mat4_mult(MV, V, W); mat4_mult(MVP, P, MV);
    glUniformMatrix4fv(mp.mvp_loc, 1, GL_FALSE, MVP);
    glUniformMatrix4fv(mp.mv_loc, 1, GL_FALSE, MV);
    glDepthMask(GL_TRUE);
    for (const auto& m : mp.meshes) {
        if (mp_should_hide_mesh(m)) continue;
        if (!m.vao || m.index_count == 0 || m.has_alpha) continue;
        if (mp.selected_lod >= 0 &&
            m.lod_index != (uint32_t)mp.selected_lod) continue;
        glDisable(GL_BLEND);
        unsigned int diffuse_to_use  = (m.diffuse_visible  && m.tex_diffuse)  ? m.tex_diffuse  : mp.default_tex;
        unsigned int normal_to_use   = (m.normal_visible   && m.tex_normal)   ? m.tex_normal   : mp.default_tex;
        unsigned int specular_to_use = (m.specular_visible && m.tex_specular) ? m.tex_specular : mp.default_tex;
        unsigned int metallic_to_use = (m.metallic_visible && m.tex_metallic) ? m.tex_metallic : mp.default_tex;
        unsigned int extra_to_use    = (m.extra_visible    && m.tex_extra)    ? m.tex_extra    : mp.default_tex;
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, diffuse_to_use); glUniform1i(mp.tex_diffuse_loc, 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, normal_to_use); glUniform1i(mp.tex_normal_loc, 1);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, specular_to_use); glUniform1i(mp.tex_specular_loc, 2);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, metallic_to_use); glUniform1i(mp.tex_metallic_loc, 3);
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, extra_to_use); glUniform1i(mp.tex_extra_loc, 4);
        glBindVertexArray(m.vao); glDrawElements(GL_TRIANGLES, m.index_count, GL_UNSIGNED_INT, 0); glBindVertexArray(0);
    }
    glDepthMask(GL_FALSE);
    for (const auto& m : mp.meshes) {
        if (mp_should_hide_mesh(m)) continue;
        if (!m.vao || m.index_count == 0 || !m.has_alpha) continue;
        if (mp.selected_lod >= 0 &&
            m.lod_index != (uint32_t)mp.selected_lod) continue;
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        unsigned int diffuse_to_use  = (m.diffuse_visible  && m.tex_diffuse)  ? m.tex_diffuse  : mp.default_tex;
        unsigned int normal_to_use   = (m.normal_visible   && m.tex_normal)   ? m.tex_normal   : mp.default_tex;
        unsigned int specular_to_use = (m.specular_visible && m.tex_specular) ? m.tex_specular : mp.default_tex;
        unsigned int metallic_to_use = (m.metallic_visible && m.tex_metallic) ? m.tex_metallic : mp.default_tex;
        unsigned int extra_to_use    = (m.extra_visible    && m.tex_extra)    ? m.tex_extra    : mp.default_tex;
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, diffuse_to_use); glUniform1i(mp.tex_diffuse_loc, 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, normal_to_use); glUniform1i(mp.tex_normal_loc, 1);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, specular_to_use); glUniform1i(mp.tex_specular_loc, 2);
        glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, metallic_to_use); glUniform1i(mp.tex_metallic_loc, 3);
        glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_2D, extra_to_use); glUniform1i(mp.tex_extra_loc, 4);
        glBindVertexArray(m.vao); glDrawElements(GL_TRIANGLES, m.index_count, GL_UNSIGNED_INT, 0); glBindVertexArray(0);
    }
    glDepthMask(GL_TRUE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
unsigned int MP_GetTexture(ModelPreview& mp) { return mp.color_tex; }
