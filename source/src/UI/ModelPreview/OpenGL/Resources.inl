static const char* gl_vs = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
uniform mat4 uMVP;
uniform mat4 uMV;
out vec3 vNormal;
out vec2 vTexCoord;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNormal = normalize(mat3(uMV) * aNormal);
    vTexCoord = aTexCoord;
}
)";
static const char* gl_fs = R"(
#version 330 core
in vec3 vNormal;
in vec2 vTexCoord;
uniform sampler2D uTexDiffuse;
uniform sampler2D uTexNormal;
uniform sampler2D uTexSpecular;
uniform sampler2D uTexMetallic;
uniform sampler2D uTexExtra;
out vec4 FragColor;
void main() {

    vec4 diffSamp = texture(uTexDiffuse, vTexCoord);
    vec3 albedo = diffSamp.rgb;
    float alpha = diffSamp.a;

    vec3 nSamp = texture(uTexNormal, vTexCoord).rgb;
    bool nIsDefault = (nSamp.r > 0.98 && nSamp.g > 0.98 && nSamp.b > 0.98);
    vec3 N_geo = normalize(vNormal);
    vec3 N = N_geo;
    if (!nIsDefault) {
        vec3 N_m = nSamp * 2.0 - 1.0;
        N = normalize(N_geo + N_m * 0.5);
    }

    vec3 L = normalize(vec3(0.3, 0.7, 0.5));
    vec3 V = vec3(0.0, 0.0, 1.0);
    vec3 H = normalize(L + V);

    float ndotl = abs(dot(N, L));
    float diff_term = 0.55 + 0.45 * ndotl;

    vec3 sSamp = texture(uTexSpecular, vTexCoord).rgb;
    bool sIsDefault = (sSamp.r > 0.98 && sSamp.g > 0.98 && sSamp.b > 0.98);
    float spec_mask = sIsDefault ? 0.0 : sSamp.r;
    float ndoth = clamp(abs(dot(N, H)), 0.0, 1.0);
    float spec = pow(ndoth, 24.0) * spec_mask * 0.6;

    vec3 color = albedo * diff_term + vec3(spec);
    FragColor = vec4(color, alpha);
}
)";
static unsigned int compile_gl_shader(const char* src, GLenum type) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) { glDeleteShader(shader); return 0; }
    return shader;
}
static unsigned int create_gl_program(const char* vs_src, const char* fs_src) {
    unsigned int vs = compile_gl_shader(vs_src, GL_VERTEX_SHADER);
    if (!vs) return 0;
    unsigned int fs = compile_gl_shader(fs_src, GL_FRAGMENT_SHADER);
    if (!fs) { glDeleteShader(vs); return 0; }
    unsigned int prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    int success;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) { glDeleteProgram(prog); return 0; }
    return prog;
}
unsigned int create_gl_texture_from_rgba(int w, int h, const uint8_t* data) {
    unsigned int tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    float maxAniso = 0.0f;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso);
    if (maxAniso > 0.0f) { glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(maxAniso, 16.0f)); }
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}
static unsigned int create_white_tex() {
    uint32_t px = 0xFFFFFFFF;
    return create_gl_texture_from_rgba(1, 1, (const uint8_t*)&px);
}
static void mp_release_mesh_gl(MPPerMesh& m) {
    if (m.vao) { glDeleteVertexArrays(1, &m.vao); m.vao = 0; }
    if (m.vbo) { glDeleteBuffers(1, &m.vbo); m.vbo = 0; }
    if (m.ibo) { glDeleteBuffers(1, &m.ibo); m.ibo = 0; }
    if (m.tex_diffuse) { glDeleteTextures(1, &m.tex_diffuse); m.tex_diffuse = 0; }
    if (m.tex_normal) { glDeleteTextures(1, &m.tex_normal); m.tex_normal = 0; }
    if (m.tex_specular) { glDeleteTextures(1, &m.tex_specular); m.tex_specular = 0; }
    if (m.tex_metallic) { glDeleteTextures(1, &m.tex_metallic); m.tex_metallic = 0; }
    if (m.tex_extra) { glDeleteTextures(1, &m.tex_extra); m.tex_extra = 0; }
    m.index_count = 0;
}
static void mp_release_gl(ModelPreview& mp) {
    for (auto& m : mp.meshes) mp_release_mesh_gl(m);
    mp.meshes.clear();
    if (mp.fbo) { glDeleteFramebuffers(1, &mp.fbo); mp.fbo = 0; }
    if (mp.color_tex) { glDeleteTextures(1, &mp.color_tex); mp.color_tex = 0; }
    if (mp.depth_rbo) { glDeleteRenderbuffers(1, &mp.depth_rbo); mp.depth_rbo = 0; }
    if (mp.shader_program) { glDeleteProgram(mp.shader_program); mp.shader_program = 0; }
    if (mp.default_tex) { glDeleteTextures(1, &mp.default_tex); mp.default_tex = 0; }
    mp.has_model = false;
}
