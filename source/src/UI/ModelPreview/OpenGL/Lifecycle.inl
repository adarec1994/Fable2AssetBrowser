bool MP_Init(ModelPreview& mp, int w, int h) {
    mp.width = w;
    mp.height = h;
    mp.shader_program = create_gl_program(gl_vs, gl_fs);
    if (!mp.shader_program) return false;
    mp.mvp_loc = glGetUniformLocation(mp.shader_program, "uMVP");
    mp.mv_loc = glGetUniformLocation(mp.shader_program, "uMV");
    mp.tex_diffuse_loc = glGetUniformLocation(mp.shader_program, "uTexDiffuse");
    mp.tex_normal_loc = glGetUniformLocation(mp.shader_program, "uTexNormal");
    mp.tex_specular_loc = glGetUniformLocation(mp.shader_program, "uTexSpecular");
    mp.tex_metallic_loc = glGetUniformLocation(mp.shader_program, "uTexMetallic");
    mp.tex_extra_loc = glGetUniformLocation(mp.shader_program, "uTexExtra");
    glGenFramebuffers(1, &mp.fbo);
    glGenTextures(1, &mp.color_tex);
    glGenRenderbuffers(1, &mp.depth_rbo);
    glBindFramebuffer(GL_FRAMEBUFFER, mp.fbo);
    glBindTexture(GL_TEXTURE_2D, mp.color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mp.color_tex, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, mp.depth_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, mp.depth_rbo);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    mp.default_tex = create_white_tex();
    return true;
}
void MP_Release(ModelPreview& mp) {
    mp_release_gl(mp);
}
void MP_Resize(ModelPreview& mp, int w, int h) {
    if (w == mp.width && h == mp.height) return;
    mp.width = w;
    mp.height = h;
    glBindTexture(GL_TEXTURE_2D, mp.color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindRenderbuffer(GL_RENDERBUFFER, mp.depth_rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
}
