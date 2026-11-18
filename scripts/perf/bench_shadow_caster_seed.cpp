// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
// The per-draw cost of the Stochastic shadows' caster seed (doc/shadows/shadow-method.md, Cost). Standalone, Windows, offscreen
// GL; not part of the viewer build. Every blended caster draw sets st_seed (LLPipeline::renderAlphaObjects), and the seed
// changes from draw to draw, so LLGLSLShader's value cache (an unordered_map of location to last value, mimicked here) never
// keeps the glUniform1i from the driver. N tiny depth-only draws, as many as the blended caster draws of the Rooftops
// cascades, each binding its own texture as pushBatch does: A sets two float uniforms that hit the cache (the stock per-draw
// sets), B also sets the changing seed. Interleaved frames, medians. It prints the marginal time per draw on the app thread,
// to the end of glFinish (the driver's own thread included) and on the GPU (GL_TIME_ELAPSED, which counts the GPU's waits
// for the driver too).
//
// Build and run (x64 Native Tools prompt):
//   cl /nologo /std:c++20 /O2 /EHsc /W3 bench_shadow_caster_seed.cpp opengl32.lib user32.lib gdi32.lib
//   bench_shadow_caster_seed.exe
#include <windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <unordered_map>
#include <vector>

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_ARRAY_BUFFER 0x8892
#define GL_STATIC_DRAW 0x88E4
#define GL_FRAMEBUFFER 0x8D40
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82

#define FN(ret, name, ...) typedef ret (APIENTRY *PFN_##name)(__VA_ARGS__); static PFN_##name name;
FN(GLuint, glCreateShader, GLenum)
FN(void, glShaderSource, GLuint, GLsizei, const GLchar* const*, const GLint*)
FN(void, glCompileShader, GLuint)
FN(void, glGetShaderiv, GLuint, GLenum, GLint*)
FN(void, glGetShaderInfoLog, GLuint, GLsizei, GLsizei*, GLchar*)
FN(GLuint, glCreateProgram, void)
FN(void, glAttachShader, GLuint, GLuint)
FN(void, glLinkProgram, GLuint)
FN(void, glGetProgramiv, GLuint, GLenum, GLint*)
FN(void, glUseProgram, GLuint)
FN(GLint, glGetUniformLocation, GLuint, const GLchar*)
FN(void, glUniform1i, GLint, GLint)
FN(void, glUniform1f, GLint, GLfloat)
FN(void, glGenFramebuffers, GLsizei, GLuint*)
FN(void, glBindFramebuffer, GLenum, GLuint)
FN(void, glFramebufferTexture2D, GLenum, GLenum, GLenum, GLuint, GLint)
FN(void, glGenVertexArrays, GLsizei, GLuint*)
FN(void, glBindVertexArray, GLuint)
FN(void, glGenBuffers, GLsizei, GLuint*)
FN(void, glBindBuffer, GLenum, GLuint)
FN(void, glBufferData, GLenum, GLsizeiptr, const void*, GLenum)
FN(void, glVertexAttribPointer, GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)
FN(void, glEnableVertexAttribArray, GLuint)
FN(void, glGenQueries, GLsizei, GLuint*)
FN(void, glBeginQuery, GLenum, GLuint)
FN(void, glEndQuery, GLenum)
FN(void, glGetQueryObjectui64v, GLuint, GLenum, unsigned long long*)
FN(void, glActiveTexture, GLenum)

static void* load(const char* n)
{
    void* p = (void*)wglGetProcAddress(n);
    if (!p) { std::printf("missing %s\n", n); std::exit(2); }
    return p;
}

struct Cache   // LLGLSLShader::mValue
{
    std::unordered_map<GLint, float> v;
    void set1i(GLint loc, GLint x)
    {
        auto it = v.find(loc);
        if (it == v.end() || it->second != x) { glUniform1i(loc, x); v[loc] = (float)x; }
    }
    void set1f(GLint loc, float x)
    {
        auto it = v.find(loc);
        if (it == v.end() || it->second != x) { glUniform1f(loc, x); v[loc] = x; }
    }
};

static double now_us()
{
    static LARGE_INTEGER f = [] { LARGE_INTEGER q; QueryPerformanceFrequency(&q); return q; }();
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart * 1e6 / (double)f.QuadPart;
}

int main()
{
    HWND w = CreateWindowExW(0, L"STATIC", L"bench", 0, 0, 0, 32, 32, nullptr, nullptr, nullptr, nullptr);
    HDC dc = GetDC(w);
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL, PFD_TYPE_RGBA, 24 };
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC rc = wglCreateContext(dc);
    wglMakeCurrent(dc, rc);
    std::printf("GL: %s %s\n", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
#define L(n) n = (PFN_##n)load(#n);
    L(glCreateShader) L(glShaderSource) L(glCompileShader) L(glGetShaderiv) L(glGetShaderInfoLog) L(glCreateProgram)
    L(glAttachShader) L(glLinkProgram) L(glGetProgramiv) L(glUseProgram) L(glGetUniformLocation) L(glUniform1i) L(glUniform1f)
    L(glGenFramebuffers) L(glBindFramebuffer) L(glFramebufferTexture2D) L(glGenVertexArrays) L(glBindVertexArray)
    L(glGenBuffers) L(glBindBuffer) L(glBufferData) L(glVertexAttribPointer) L(glEnableVertexAttribArray)
    L(glGenQueries) L(glBeginQuery) L(glEndQuery) L(glGetQueryObjectui64v) L(glActiveTexture)

    const char* vs = "#version 420\nlayout(location=0) in vec3 position;\nvoid main(){ gl_Position = vec4(position, 1.0); }\n";
    const char* fs = "#version 420\nuniform int st_seed; uniform float a; uniform float b; uniform sampler2D tex0; out vec4 c;\n"
                     "void main(){ if ((((st_seed ^ int(gl_FragCoord.x)) & 1) == 0) && a > b + texture(tex0, vec2(0.5)).a) discard; c = vec4(1.0); }\n";
    GLuint p = glCreateProgram();
    for (auto [kind, src] : { std::pair<GLenum, const char*>{ GL_VERTEX_SHADER, vs }, { GL_FRAGMENT_SHADER, fs } })
    {
        GLuint s = glCreateShader(kind); glShaderSource(s, 1, &src, nullptr); glCompileShader(s);
        GLint okc = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &okc);
        if (!okc) { char log[2048]; glGetShaderInfoLog(s, 2048, nullptr, log); std::printf("%s\n", log); return 1; }
        glAttachShader(p, s);
    }
    glLinkProgram(p);
    GLint okl = 0; glGetProgramiv(p, GL_LINK_STATUS, &okl);
    if (!okl) { std::printf("link failed\n"); return 1; }
    glUseProgram(p);
    const GLint lseed = glGetUniformLocation(p, "st_seed"), la = glGetUniformLocation(p, "a"), lb = glGetUniformLocation(p, "b");
    if (lseed < 0 || la < 0 || lb < 0) { std::printf("inactive uniform\n"); return 1; }

    GLuint tex = 0; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 256, 256, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    GLuint fbo = 0; glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, tex, 0);
    glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
    glViewport(0, 0, 256, 256); glEnable(GL_DEPTH_TEST);
    GLuint vao = 0; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    const float tri[] = { -0.01f, -0.01f, 0.f, 0.01f, -0.01f, 0.f, -0.01f, 0.01f, 0.f };
    GLuint vbo = 0; glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr); glEnableVertexAttribArray(0);

    // 64 small RGBA textures, one bound per draw in turn (pushBatch binds each face's diffuse map)
    GLuint texs[64];
    glGenTextures(64, texs);
    std::vector<unsigned char> px(16 * 16 * 4, 200);
    for (GLuint t : texs)
    {
        glBindTexture(GL_TEXTURE_2D, t);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    }
    glUniform1i(glGetUniformLocation(p, "tex0"), 0);
    GLuint q = 0; glGenQueries(1, &q);
    std::vector<double> ga, gb;
    const int N = 4399;   // the Rooftops cascades' blended caster draws (doc/shadows/sun-shadow-cost-study.md)
    std::vector<GLint> seeds(N);
    uint32_t x = 12345u;
    for (GLint& s : seeds) { x = x * 747796405u + 2891336453u; s = (GLint)x; }
    Cache cache;
    std::vector<double> ta, tb, fa, fb;
    for (int frame = 0; frame < 230; ++frame)
    {
        for (int variant = 0; variant < 2; ++variant)
        {
            glClear(GL_DEPTH_BUFFER_BIT);
            glFinish();
            glBeginQuery(0x88BF, q);
            const double t0 = now_us();
            for (int i = 0; i < N; ++i)
            {
                glBindTexture(GL_TEXTURE_2D, texs[i & 63]);
                cache.set1f(la, 0.5f);
                cache.set1f(lb, 0.1f);
                if (variant == 1)
                {
                    cache.set1i(lseed, seeds[i]);
                }
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
            const double t1 = now_us();
            glEndQuery(0x88BF);
            glFinish();
            const double t2 = now_us();
            if (frame >= 30)
            {
                (variant ? tb : ta).push_back(t1 - t0);
                (variant ? fb : fa).push_back(t2 - t0);
                unsigned long long ns = 0; glGetQueryObjectui64v(q, 0x8866, &ns);
                (variant ? gb : ga).push_back((double)ns / 1000.0);
            }
        }
    }
    auto median = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
    const double a = median(ta), b = median(tb);
    std::printf("%d draws per frame, 200 frames each, interleaved: A (texture bind + 2 cached uniforms + draw) median %.1f us, "
                "B (A + the changing seed) median %.1f us: +%.1f ns per draw, +%.3f ms per frame of %d draws\n",
                N, a, b, (b - a) * 1000.0 / N, (b - a) / 1000.0, N);
    const double fa_ = median(fa), fb_ = median(fb);
    std::printf("  to the end of glFinish (the driver's own thread included): A %.1f us, B %.1f us: +%.1f ns per draw\n",
                fa_, fb_, (fb_ - fa_) * 1000.0 / N);
    const double ga_ = median(ga), gb_ = median(gb);
    std::printf("  GPU time (GL_TIME_ELAPSED): A %.1f us, B %.1f us: +%.1f ns per draw\n", ga_, gb_, (gb_ - ga_) * 1000.0 / N);
    wglMakeCurrent(nullptr, nullptr); wglDeleteContext(rc); ReleaseDC(w, dc); DestroyWindow(w);
    return 0;
}
