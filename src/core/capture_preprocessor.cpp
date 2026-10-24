#include <bitloop/core/capture_preprocessor.h>

#include <algorithm>
#include <cstring>
#include <string>

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include "glad/glad.h"
#endif

BL_BEGIN_NS;

static bool gl_has_current_context()
{
    #ifndef __EMSCRIPTEN__
    if (!glad_glGetString)
        return false;
    #endif

    const GLubyte* v = glGetString(GL_VERSION);
    return v != nullptr;
}

static void gl_delete_program(GLuint& p)
{
    if (p) { glDeleteProgram(p); p = 0; }
}

static void gl_delete_shader(GLuint& s)
{
    if (s) { glDeleteShader(s); s = 0; }
}

static void gl_delete_texture(GLuint& t)
{
    if (t) { glDeleteTextures(1, &t); t = 0; }
}

static void gl_delete_fbo(GLuint& f)
{
    if (f) { glDeleteFramebuffers(1, &f); f = 0; }
}

static void gl_delete_vao(GLuint& v)
{
    if (v) { glDeleteVertexArrays(1, &v); v = 0; }
}

struct ScopedGLState
{
    GLint prev_fbo = 0;
    GLint prev_program = 0;
    GLint prev_vao = 0;

    GLint prev_active_tex = 0;
    GLint prev_tex2d_active = 0;
    GLint prev_tex2d0 = 0;

    GLint prev_viewport[4] = { 0, 0, 0, 0 };
    GLint prev_pack = 0;
    GLint prev_unpack = 0;

    GLboolean depth_test = GL_FALSE;
    GLboolean blend = GL_FALSE;
    GLboolean scissor = GL_FALSE;
    GLboolean cull = GL_FALSE;

    #if defined(GL_FRAMEBUFFER_SRGB)
    GLboolean framebuffer_srgb = GL_FALSE;
    #endif

    ScopedGLState()
    {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
        glGetIntegerv(GL_VIEWPORT, prev_viewport);
        glGetIntegerv(GL_PACK_ALIGNMENT, &prev_pack);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_unpack);

        glGetIntegerv(GL_ACTIVE_TEXTURE, &prev_active_tex);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex2d_active);

        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev_tex2d0);
        glActiveTexture((GLenum)prev_active_tex);

        depth_test = glIsEnabled(GL_DEPTH_TEST);
        blend = glIsEnabled(GL_BLEND);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
        cull = glIsEnabled(GL_CULL_FACE);

        #if defined(GL_FRAMEBUFFER_SRGB)
        framebuffer_srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
        #endif
    }

    ~ScopedGLState()
    {
        #if defined(GL_FRAMEBUFFER_SRGB)
        if (framebuffer_srgb) glEnable(GL_FRAMEBUFFER_SRGB);
        else glDisable(GL_FRAMEBUFFER_SRGB);
        #endif

        if (depth_test) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);

        glPixelStorei(GL_PACK_ALIGNMENT, prev_pack);
        glPixelStorei(GL_UNPACK_ALIGNMENT, prev_unpack);

        glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);

        glUseProgram(prev_program);
        glBindVertexArray(prev_vao);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex2d0);

        glActiveTexture((GLenum)prev_active_tex);
        glBindTexture(GL_TEXTURE_2D, (GLuint)prev_tex2d_active);

        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    }
};

static bool compile_shader(GLuint& out_shader, GLenum stage, const char* src)
{
    out_shader = glCreateShader(stage);
    glShaderSource(out_shader, 1, &src, nullptr);
    glCompileShader(out_shader);

    GLint ok = 0;
    glGetShaderiv(out_shader, GL_COMPILE_STATUS, &ok);
    if (ok) return true;

    GLint log_len = 0;
    glGetShaderiv(out_shader, GL_INFO_LOG_LENGTH, &log_len);
    std::string log;
    log.resize(std::max(0, log_len));
    if (log_len > 0)
        glGetShaderInfoLog(out_shader, log_len, nullptr, log.data());

    blPrint() << "CapturePreprocessor shader compile failed:\n" << log;
    gl_delete_shader(out_shader);
    return false;
}

static bool link_program(GLuint& out_prog, GLuint vs, GLuint fs)
{
    out_prog = glCreateProgram();
    glAttachShader(out_prog, vs);
    glAttachShader(out_prog, fs);
    glLinkProgram(out_prog);

    GLint ok = 0;
    glGetProgramiv(out_prog, GL_LINK_STATUS, &ok);
    if (ok) return true;

    GLint log_len = 0;
    glGetProgramiv(out_prog, GL_INFO_LOG_LENGTH, &log_len);
    std::string log;
    log.resize(std::max(0, log_len));
    if (log_len > 0)
        glGetProgramInfoLog(out_prog, log_len, nullptr, log.data());

    blPrint() << "CapturePreprocessor program link failed:\n" << log;
    gl_delete_program(out_prog);
    return false;
}

static void tex_params_nearest_clamp(GLuint tex)
{
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

CapturePreprocessor::~CapturePreprocessor()
{
    // GL object deletion requires a current context; provide explicit destroyGL() for deterministic teardown
    if (gl_has_current_context())
        destroyGL();
}

void CapturePreprocessor::destroyGL()
{
    gl_delete_program(prog_down);
    gl_delete_program(prog_unsharp);

    gl_delete_fbo(fbo_down);
    gl_delete_fbo(fbo_out);

    gl_delete_texture(upload_tex);
    gl_delete_texture(down_tex);
    gl_delete_texture(output_tex);

    gl_delete_vao(vao);

    initialized = false;
    upload_size = {};
    down_size = {};
    target_size = {};
}

void CapturePreprocessor::updateShaderVersionStrings()
{
    is_gles = false;

    const char* glsl = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    if (!glsl) return;
    const std::string s(glsl);
    if (s.find("ES") != std::string::npos)
        is_gles = true;
}

bool CapturePreprocessor::compilePrograms()
{
    updateShaderVersionStrings();

    const char* vs_common =
        "\n"
        "void main() {\n"
        "    vec2 pos;\n"
        "    if (gl_VertexID == 0) pos = vec2(-1.0, -1.0);\n"
        "    else if (gl_VertexID == 1) pos = vec2(3.0, -1.0);\n"
        "    else pos = vec2(-1.0, 3.0);\n"
        "    gl_Position = vec4(pos, 0.0, 1.0);\n"
        "}\n";

    const char* fs_srgb =
        "vec3 srgb_to_linear(vec3 c) {\n"
        "    bvec3 lo = lessThanEqual(c, vec3(0.04045));\n"
        "    vec3 a = c / 12.92;\n"
        "    vec3 b = pow((c + 0.055) / 1.055, vec3(2.4));\n"
        "    return mix(b, a, lo);\n"
        "}\n"
        "vec3 linear_to_srgb(vec3 c) {\n"
        "    c = clamp(c, vec3(0.0), vec3(1.0));\n"
        "    bvec3 lo = lessThanEqual(c, vec3(0.0031308));\n"
        "    vec3 a = c * 12.92;\n"
        "    vec3 b = 1.055 * pow(c, vec3(1.0/2.4)) - 0.055;\n"
        "    return mix(b, a, lo);\n"
        "}\n";

    std::string vs;
    std::string fs_down;
    std::string fs_unsharp;

    if (is_gles)
    {
        vs = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
        fs_down = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
        fs_unsharp = "#version 300 es\nprecision highp float;\nprecision highp int;\n";
    }
    else
    {
        vs = "#version 330 core\n";
        fs_down = "#version 330 core\n";
        fs_unsharp = "#version 330 core\n";
    }

    vs += vs_common;

    fs_down += "uniform sampler2D uSrc;\n";
    fs_down += "uniform ivec2 uSrcSize;\n";
    fs_down += "uniform int uSsaa;\n";
    fs_down += "uniform int uFlipY;\n";
    fs_down += is_gles ? "layout(location=0) out vec4 oColor;\n" : "layout(location=0) out vec4 oColor;\n";
    fs_down += fs_srgb;
    fs_down +=
        "\n"
        "void main() {\n"
        "    ivec2 dst = ivec2(gl_FragCoord.xy);\n"
        "    int N = clamp(uSsaa, 1, 16);\n"
        "    int baseX = dst.x * N;\n"
        "    int baseY = dst.y * N;\n"
        "    if (uFlipY != 0) baseY = uSrcSize.y - (dst.y + 1) * N;\n"
        "\n"
        "    vec3 acc = vec3(0.0);\n"
        "    float accA = 0.0;\n"
        "    int count = N * N;\n"
        "    for (int j = 0; j < 16; ++j) {\n"
        "        if (j >= N) break;\n"
        "        for (int i = 0; i < 16; ++i) {\n"
        "            if (i >= N) break;\n"
        "            ivec2 sp = ivec2(baseX + i, baseY + j);\n"
        "            sp = clamp(sp, ivec2(0), uSrcSize - ivec2(1));\n"
        "            vec4 c = texelFetch(uSrc, sp, 0);\n"
        "            acc += srgb_to_linear(c.rgb);\n"
        "            accA += c.a;\n"
        "        }\n"
        "    }\n"
        "\n"
        "    vec3 rgb = acc / float(count);\n"
        "    float a = accA / float(count);\n"
        "    oColor = vec4(linear_to_srgb(rgb), a);\n"
        "}\n";

    fs_unsharp += "uniform sampler2D uTex;\n";
    fs_unsharp += "uniform ivec2 uSize;\n";
    fs_unsharp += "uniform float uAmount;\n";
    fs_unsharp += "layout(location=0) out vec4 oColor;\n";
    fs_unsharp += fs_srgb;
    fs_unsharp +=
        "\n"
        "void main() {\n"
        "    ivec2 p = ivec2(gl_FragCoord.xy);\n"
        "    p = clamp(p, ivec2(0), uSize - ivec2(1));\n"
        "    vec4 c0 = texelFetch(uTex, p, 0);\n"
        "    float amount = clamp(uAmount, 0.0, 1.0);\n"
        "    if (amount <= 0.0) { oColor = c0; return; }\n"
        "\n"
        "    vec3 center = srgb_to_linear(c0.rgb);\n"
        "    vec3 blur = vec3(0.0);\n"
        "    for (int j = -1; j <= 1; ++j)\n"
        "    for (int i = -1; i <= 1; ++i) {\n"
        "        ivec2 q = clamp(p + ivec2(i, j), ivec2(0), uSize - ivec2(1));\n"
        "        vec4 c = texelFetch(uTex, q, 0);\n"
        "        blur += srgb_to_linear(c.rgb);\n"
        "    }\n"
        "    blur *= (1.0 / 9.0);\n"
        "    vec3 sharp = clamp(center + amount * (center - blur), vec3(0.0), vec3(1.0));\n"
        "    oColor = vec4(linear_to_srgb(sharp), c0.a);\n"
        "}\n";

    GLuint vs_id = 0, fs_down_id = 0, fs_unsharp_id = 0;
    GLuint new_prog_down = 0, new_prog_unsharp = 0;

    if (!compile_shader(vs_id, GL_VERTEX_SHADER, vs.c_str()))
        return false;
    if (!compile_shader(fs_down_id, GL_FRAGMENT_SHADER, fs_down.c_str()))
    {
        gl_delete_shader(vs_id);
        return false;
    }
    if (!compile_shader(fs_unsharp_id, GL_FRAGMENT_SHADER, fs_unsharp.c_str()))
    {
        gl_delete_shader(vs_id);
        gl_delete_shader(fs_down_id);
        return false;
    }

    if (!link_program(new_prog_down, vs_id, fs_down_id))
    {
        gl_delete_shader(vs_id);
        gl_delete_shader(fs_down_id);
        gl_delete_shader(fs_unsharp_id);
        return false;
    }
    if (!link_program(new_prog_unsharp, vs_id, fs_unsharp_id))
    {
        gl_delete_program(prog_down);
        gl_delete_shader(vs_id);
        gl_delete_shader(fs_down_id);
        gl_delete_shader(fs_unsharp_id);
        return false;
    }

    gl_delete_shader(vs_id);
    gl_delete_shader(fs_down_id);
    gl_delete_shader(fs_unsharp_id);

    // replace any existing programs only once both compile successfully
    gl_delete_program(prog_down);
    gl_delete_program(prog_unsharp);

    prog_down = new_prog_down;
    prog_unsharp = new_prog_unsharp;

    loc_down_src = glGetUniformLocation(prog_down, "uSrc");
    loc_down_srcSize = glGetUniformLocation(prog_down, "uSrcSize");
    loc_down_ssaa = glGetUniformLocation(prog_down, "uSsaa");
    loc_down_flipY = glGetUniformLocation(prog_down, "uFlipY");

    loc_unsharp_tex = glGetUniformLocation(prog_unsharp, "uTex");
    loc_unsharp_size = glGetUniformLocation(prog_unsharp, "uSize");
    loc_unsharp_amount = glGetUniformLocation(prog_unsharp, "uAmount");

    return true;
}

bool CapturePreprocessor::ensureInitialized()
{
    if (initialized)
        return true;

    if (!gl_has_current_context())
    {
        blPrint() << "CapturePreprocessor: no current GL context";
        return false;
    }

    glGenVertexArrays(1, reinterpret_cast<GLuint*>(&vao));
    glBindVertexArray((GLuint)vao);

    if (!compilePrograms())
    {
        destroyGL();
        return false;
    }

    glGenFramebuffers(1, reinterpret_cast<GLuint*>(&fbo_down));
    glGenFramebuffers(1, reinterpret_cast<GLuint*>(&fbo_out));

    initialized = true;
    return true;
}

bool CapturePreprocessor::ensureUploadTexture(const IVec2& src_resolution)
{
    if (upload_tex != 0 && upload_size.x == src_resolution.x && upload_size.y == src_resolution.y)
        return true;

    gl_delete_texture(reinterpret_cast<GLuint&>(upload_tex));
    glGenTextures(1, reinterpret_cast<GLuint*>(&upload_tex));
    tex_params_nearest_clamp((GLuint)upload_tex);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
        src_resolution.x, src_resolution.y,
        0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    upload_size = src_resolution;
    return true;
}

bool CapturePreprocessor::ensureDownTarget(const IVec2& dst_resolution)
{
    if (down_tex != 0 && down_size.x == dst_resolution.x && down_size.y == dst_resolution.y)
        return true;

    gl_delete_texture(reinterpret_cast<GLuint&>(down_tex));

    glGenTextures(1, reinterpret_cast<GLuint*>(&down_tex));
    tex_params_nearest_clamp((GLuint)down_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
        dst_resolution.x, dst_resolution.y,
        0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo_down);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, (GLuint)down_tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        blPrint() << "CapturePreprocessor: downscale FBO incomplete";
        return false;
    }

    down_size = dst_resolution;
    return true;
}

bool CapturePreprocessor::ensureOutputTarget(const IVec2& dst_resolution)
{
    if (output_tex != 0 && target_size.x == dst_resolution.x && target_size.y == dst_resolution.y)
        return true;

    gl_delete_texture(reinterpret_cast<GLuint&>(output_tex));

    glGenTextures(1, reinterpret_cast<GLuint*>(&output_tex));
    tex_params_nearest_clamp((GLuint)output_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
        dst_resolution.x, dst_resolution.y,
        0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo_out);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, (GLuint)output_tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        blPrint() << "CapturePreprocessor: output FBO incomplete";
        return false;
    }

    target_size = dst_resolution;
    return true;
}

bool CapturePreprocessor::runPipeline(uint32_t src_texture, const CapturePreprocessParams& params, bytebuf* out_rgba, uint32_t dst_fbo_override)
{
    ScopedGLState _gl;

    if (!ensureInitialized())
        return false;

    if (params.dst_resolution.x <= 0 || params.dst_resolution.y <= 0)
        return false;
    if (params.src_resolution.x <= 0 || params.src_resolution.y <= 0)
        return false;

    const bool use_external_output = (dst_fbo_override != 0);
    const bool need_sharpen = (params.sharpen > 0.0f);

    if (need_sharpen)
    {
        if (!ensureDownTarget(params.dst_resolution))
            return false;
    }

    if (!use_external_output)
    {
        if (!ensureOutputTarget(params.dst_resolution))
            return false;
    }

#if defined(GL_FRAMEBUFFER_SRGB)
    glDisable(GL_FRAMEBUFFER_SRGB);
#endif
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);

    glBindVertexArray((GLuint)vao);
    glActiveTexture(GL_TEXTURE0);

    const GLuint final_fbo = use_external_output ? (GLuint)dst_fbo_override : (GLuint)fbo_out;

    // downscale pass
    glUseProgram((GLuint)prog_down);
    glBindTexture(GL_TEXTURE_2D, (GLuint)src_texture);
    if (loc_down_src >= 0) glUniform1i(loc_down_src, 0);
    if (loc_down_srcSize >= 0) glUniform2i(loc_down_srcSize, params.src_resolution.x, params.src_resolution.y);
    if (loc_down_ssaa >= 0) glUniform1i(loc_down_ssaa, params.ssaa);
    if (loc_down_flipY >= 0) glUniform1i(loc_down_flipY, params.flip_y ? 1 : 0);

    if (!need_sharpen)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, final_fbo);
        glViewport(0, 0, params.dst_resolution.x, params.dst_resolution.y);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    else
    {
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fbo_down);
        glViewport(0, 0, params.dst_resolution.x, params.dst_resolution.y);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        // unsharp pass
        glBindFramebuffer(GL_FRAMEBUFFER, final_fbo);
        glViewport(0, 0, params.dst_resolution.x, params.dst_resolution.y);
        glUseProgram((GLuint)prog_unsharp);
        glBindTexture(GL_TEXTURE_2D, (GLuint)down_tex);
        if (loc_unsharp_tex >= 0) glUniform1i(loc_unsharp_tex, 0);
        if (loc_unsharp_size >= 0) glUniform2i(loc_unsharp_size, params.dst_resolution.x, params.dst_resolution.y);
        if (loc_unsharp_amount >= 0) glUniform1f(loc_unsharp_amount, params.sharpen);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    if (out_rgba)
    {
        out_rgba->resize((size_t)params.dst_resolution.x * (size_t)params.dst_resolution.y * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glBindFramebuffer(GL_FRAMEBUFFER, final_fbo);
        glReadPixels(0, 0, params.dst_resolution.x, params.dst_resolution.y,
            GL_RGBA, GL_UNSIGNED_BYTE, out_rgba->data());
    }

    return true;
}

bool CapturePreprocessor::preprocessToFrame(uint32_t src_texture, const CapturePreprocessParams& params, bytebuf& out_rgba)
{
    return runPipeline(src_texture, params, &out_rgba, 0);
}

bool CapturePreprocessor::preprocessToTexture(uint32_t src_texture, const CapturePreprocessParams& params)
{
    return runPipeline(src_texture, params, nullptr, 0);
}

bool CapturePreprocessor::preprocessTextureToFbo(uint32_t src_texture, const CapturePreprocessParams& params, uint32_t dst_fbo)
{
    return runPipeline(src_texture, params, nullptr, dst_fbo);
}

bool CapturePreprocessor::preprocessTextureToFbo(uint32_t src_texture, const CapturePreprocessParams& params, uint32_t dst_fbo, bytebuf& out_rgba)
{
    return runPipeline(src_texture, params, &out_rgba, dst_fbo);
}

bool CapturePreprocessor::preprocessRGBA8(const uint8_t* src_rgba, const CapturePreprocessParams& params, bytebuf& out_rgba)
{
    if (!ensureInitialized())
        return false;

    ScopedGLState _gl;

    glActiveTexture(GL_TEXTURE0);
    if (!ensureUploadTexture(params.src_resolution))
        return false;

    glBindTexture(GL_TEXTURE_2D, (GLuint)upload_tex);
    GLint prev_unpack = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_unpack);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
        params.src_resolution.x, params.src_resolution.y,
        GL_RGBA, GL_UNSIGNED_BYTE, src_rgba);
    glPixelStorei(GL_UNPACK_ALIGNMENT, prev_unpack);

    return runPipeline(upload_tex, params, &out_rgba, 0);
}

BL_END_NS;
