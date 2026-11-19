// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
// Palette upload cost: the loose uniform array of today (glUniformMatrix3x4fv, GL_FALSE) against a generated program's
// std140 block (the viewer's blockPut: memcmp + memcpy into a CPU shadow, then glNamedBufferSubData of the dirty range at
// the draw). Both programs are the rigged bump vertex stage; see bench_palette_block.py. Rounds alternate A, B, B, A, ...
// usage: bench <dir with ref.vert cand.vert ref.frag cand.frag> <rounds> <draws per frame> <grid cells>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
typedef unsigned long long GLuint64;
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_ARRAY_BUFFER 0x8892
#define GL_UNIFORM_BUFFER 0x8A11
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_STATIC_DRAW 0x88E4
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_RGBA32F 0x8814
#define GL_TIME_ELAPSED 0x88BF
#define GL_QUERY_RESULT 0x8866
#define GL_UNIFORM 0x92E1
#define GL_ACTIVE_RESOURCES 0x92F5
#define GL_NAME_LENGTH 0x92F9
#define GL_LOCATION 0x930E
#define GL_OFFSET 0x92FC
#define GL_BLOCK_INDEX 0x92FD
#define GL_BUFFER_DATA_SIZE 0x9303
#define GL_BUFFER_BINDING 0x9302
#define GL_UNIFORM_BLOCK 0x92E2

#define FN(ret, name, args) typedef ret (APIENTRY *PFN_##name) args; static PFN_##name name;
FN(GLuint, glCreateShader, (GLenum)) FN(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))
FN(void, glCompileShader, (GLuint)) FN(void, glGetShaderiv, (GLuint, GLenum, GLint*)) FN(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))
FN(GLuint, glCreateProgram, ()) FN(void, glAttachShader, (GLuint, GLuint)) FN(void, glLinkProgram, (GLuint))
FN(void, glGetProgramiv, (GLuint, GLenum, GLint*)) FN(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))
FN(void, glBindAttribLocation, (GLuint, GLuint, const GLchar*)) FN(void, glUseProgram, (GLuint))
FN(void, glGenBuffers, (GLsizei, GLuint*)) FN(void, glBindBuffer, (GLenum, GLuint)) FN(void, glBufferData, (GLenum, GLsizeiptr, const void*, GLenum))
FN(void, glNamedBufferSubData, (GLuint, GLintptr, GLsizeiptr, const void*)) FN(void, glBindBufferBase, (GLenum, GLuint, GLuint))
FN(void, glGenVertexArrays, (GLsizei, GLuint*)) FN(void, glBindVertexArray, (GLuint))
FN(void, glEnableVertexAttribArray, (GLuint)) FN(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))
FN(void, glGenFramebuffers, (GLsizei, GLuint*)) FN(void, glBindFramebuffer, (GLenum, GLuint)) FN(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))
FN(void, glUniformMatrix3x4fv, (GLint, GLsizei, GLboolean, const GLfloat*)) FN(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*))
FN(void, glGetProgramInterfaceiv, (GLuint, GLenum, GLenum, GLint*)) FN(void, glGetProgramResourceiv, (GLuint, GLenum, GLuint, GLsizei, const GLenum*, GLsizei, GLsizei*, GLint*))
FN(void, glGetProgramResourceName, (GLuint, GLenum, GLuint, GLsizei, GLsizei*, GLchar*))
FN(void, glGenQueries, (GLsizei, GLuint*)) FN(void, glBeginQuery, (GLenum, GLuint)) FN(void, glEndQuery, (GLenum)) FN(void, glGetQueryObjectui64v, (GLuint, GLenum, GLuint64*))
FN(HGLRC, wglCreateContextAttribsARB, (HDC, HGLRC, const int*))
static void* gp(const char* n) { return (void*) wglGetProcAddress(n); }
#define LOAD(n) n = (PFN_##n) gp(#n)

static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
static GLuint compile(GLenum k, const std::string& t)
{
    GLuint s = glCreateShader(k);
    const char* c = t.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char l[8192]; glGetShaderInfoLog(s, 8192, nullptr, l); fprintf(stderr, "compile: %s\n", l); exit(1); }
    return s;
}
static const char* kAttribs[] = { "position", "normal", "texcoord0", "texcoord1", "texcoord2", "texcoord3", "diffuse_color", "emissive", "tangent", "weight", "weight4" };
static GLuint link(const std::string& vs, const std::string& fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    for (int i = 0; i < 11; ++i) glBindAttribLocation(p, i, kAttribs[i]);
    glLinkProgram(p);
    GLint ok;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char l[8192]; glGetProgramInfoLog(p, 8192, nullptr, l); fprintf(stderr, "link: %s\n", l); exit(1); }
    return p;
}
struct Res { std::string name; GLint loc, offset, block; };
static std::vector<Res> uniforms(GLuint p)
{
    GLint n;
    glGetProgramInterfaceiv(p, GL_UNIFORM, GL_ACTIVE_RESOURCES, &n);
    const GLenum props[] = { GL_NAME_LENGTH, GL_LOCATION, GL_OFFSET, GL_BLOCK_INDEX };
    std::vector<Res> out;
    for (int i = 0; i < n; ++i)
    {
        GLint v[4];
        glGetProgramResourceiv(p, GL_UNIFORM, i, 4, props, 4, nullptr, v);
        std::string nm(v[0] + 1, 0);
        glGetProgramResourceName(p, GL_UNIFORM, i, v[0] + 1, nullptr, &nm[0]);
        nm.resize(strlen(nm.c_str()));
        out.push_back({ nm, v[1], v[2], v[3] });
    }
    return out;
}
static const Res* find(const std::vector<Res>& u, const char* base)
{
    std::string a = base, b = a + "[0]";
    for (auto& r : u)
    {
        if (r.name == a || r.name == b) return &r;
        size_t d = r.name.rfind('.');
        if (d != std::string::npos) { std::string t = r.name.substr(d + 1); if (t == a || t == b) return &r; }
    }
    return nullptr;
}
static double now() { using namespace std::chrono; return duration<double, std::micro>(steady_clock::now().time_since_epoch()).count(); }

struct Prog
{
    const char* label;
    GLuint p;
    bool block;
    GLint palLoc = -1, palOffset = 0, blockSize = 0;
    GLuint ubo = 0;
    std::vector<unsigned char> shadow;
    int dirtyBegin = 0, dirtyEnd = 0;
    bool dirty = false;
};
static const int kJoints = 110;
static float gPal[8][kJoints * 12];

// today: one call, the driver copies count * 48 bytes
static void putLoose(Prog& g, int idx, int count) { glUniformMatrix3x4fv(g.palLoc, count, GL_FALSE, gPal[idx]); }
// generated: blockPut (memcmp, memcpy, dirty range); a non-transposed mat3x4 array has the same bytes in std140
static void putBlock(Prog& g, int idx, int count)
{
    unsigned char* dst = g.shadow.data() + g.palOffset;
    size_t bytes = (size_t) count * 48;
    if (memcmp(dst, gPal[idx], bytes) != 0)
    {
        memcpy(dst, gPal[idx], bytes);
        g.dirtyBegin = g.dirty ? std::min<int>(g.dirtyBegin, g.palOffset) : g.palOffset;
        g.dirtyEnd = g.dirty ? std::max<int>(g.dirtyEnd, g.palOffset + (int) bytes) : g.palOffset + (int) bytes;
        g.dirty = true;
    }
}
static void flush(Prog& g)
{
    if (g.block && g.dirty)
    {
        glNamedBufferSubData(g.ubo, g.dirtyBegin, g.dirtyEnd - g.dirtyBegin, g.shadow.data() + g.dirtyBegin);
        g.dirty = false;
    }
}

int main(int argc, char** argv)
{
    if (argc < 5) { fprintf(stderr, "usage: bench dir rounds draws cells\n"); return 2; }
    std::string dir = argv[1];
    int rounds = atoi(argv[2]), draws = atoi(argv[3]), cells = atoi(argv[4]);
    HWND w = CreateWindowExW(0, L"STATIC", L"pb", 0, 0, 0, 16, 16, nullptr, nullptr, nullptr, nullptr);
    HDC dc = GetDC(w);
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, 0x24, 0, 24 };
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC legacy = wglCreateContext(dc);
    wglMakeCurrent(dc, legacy);
    LOAD(wglCreateContextAttribsARB);
    int at[] = { 0x2091, 4, 0x2092, 6, 0x9126, 1, 0x2094, 2, 0 };
    HGLRC ctx = wglCreateContextAttribsARB(dc, nullptr, at);
    if (!ctx) { fprintf(stderr, "no GL 4.6 core context\n"); return 1; }
    wglMakeCurrent(dc, ctx);
    LOAD(glCreateShader); LOAD(glShaderSource); LOAD(glCompileShader); LOAD(glGetShaderiv); LOAD(glGetShaderInfoLog); LOAD(glCreateProgram); LOAD(glAttachShader); LOAD(glLinkProgram);
    LOAD(glGetProgramiv); LOAD(glGetProgramInfoLog); LOAD(glBindAttribLocation); LOAD(glUseProgram); LOAD(glGenBuffers); LOAD(glBindBuffer); LOAD(glBufferData); LOAD(glNamedBufferSubData);
    LOAD(glBindBufferBase); LOAD(glGenVertexArrays); LOAD(glBindVertexArray); LOAD(glEnableVertexAttribArray); LOAD(glVertexAttribPointer); LOAD(glGenFramebuffers); LOAD(glBindFramebuffer);
    LOAD(glFramebufferTexture2D); LOAD(glUniformMatrix3x4fv); LOAD(glUniformMatrix4fv); LOAD(glGetProgramInterfaceiv); LOAD(glGetProgramResourceiv); LOAD(glGetProgramResourceName);
    LOAD(glGenQueries); LOAD(glBeginQuery); LOAD(glEndQuery); LOAD(glGetQueryObjectui64v);
    printf("renderer: %s\n", glGetString(GL_RENDERER));

    srand(7);
    for (auto& pal : gPal) for (float& f : pal) f = (rand() / (float) RAND_MAX - 0.5f) * 0.3f;
    for (auto& pal : gPal) for (int m = 0; m < kJoints; ++m) { pal[m * 12 + 0] += 1; pal[m * 12 + 4] += 1; pal[m * 12 + 8] += 1; }

    Prog progs[2] = { { "A today: loose uniform", link(slurp(dir + "/ref.vert"), slurp(dir + "/ref.frag")), false },
                      { "B generated: std140 block", link(slurp(dir + "/cand.vert"), slurp(dir + "/cand.frag")), true } };
    float view[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,-3,1 }, proj[16] = { 1.7f,0,0,0, 0,1.7f,0,0, 0,0,-1.002f,-1, 0,0,-0.2f,0 }, mvp[16], tex[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) { float s = 0; for (int k = 0; k < 4; ++k) s += proj[k * 4 + r] * view[c * 4 + k]; mvp[c * 4 + r] = s; }
    for (Prog& g : progs)
    {
        auto u = uniforms(g.p);
        glUseProgram(g.p);
        const Res* pal = find(u, "matrixPalette");
        if (!pal) { fprintf(stderr, "no palette uniform in %s\n", g.label); return 1; }
        g.palLoc = pal->loc;
        g.palOffset = pal->offset;
        const char* names[4] = { "modelview_matrix", "projection_matrix", "modelview_projection_matrix", "texture_matrix0" };
        const float* vals[4] = { view, proj, mvp, tex };
        if (g.block)
        {
            GLint bi = pal->block;
            const GLenum sz[] = { GL_BUFFER_DATA_SIZE };
            glGetProgramResourceiv(g.p, GL_UNIFORM_BLOCK, bi, 1, sz, 1, nullptr, &g.blockSize);
            g.shadow.assign(g.blockSize, 0);
            for (int i = 0; i < 4; ++i) if (const Res* r = find(u, names[i])) memcpy(g.shadow.data() + r->offset, vals[i], 64);
            glGenBuffers(1, &g.ubo);
            glBindBuffer(GL_UNIFORM_BUFFER, g.ubo);
            glBufferData(GL_UNIFORM_BUFFER, g.blockSize, g.shadow.data(), GL_DYNAMIC_DRAW);
            const GLenum bb[] = { GL_BUFFER_BINDING };
            GLint binding = 0;
            glGetProgramResourceiv(g.p, GL_UNIFORM_BLOCK, bi, 1, bb, 1, nullptr, &binding);
            glBindBufferBase(GL_UNIFORM_BUFFER, binding, g.ubo);
        }
        else
        {
            for (int i = 0; i < 4; ++i) if (const Res* r = find(u, names[i])) glUniformMatrix4fv(r->loc, 1, GL_FALSE, vals[i]);
        }
        printf("%s: palette at %s %d, block %d bytes\n", g.label, g.block ? "offset" : "location", g.block ? g.palOffset : g.palLoc, g.blockSize);
    }
    // a wavy grid with four joints per vertex
    std::vector<float> pos, uv0, uv1, wt;
    srand(3);
    for (int i = 0; i < cells; ++i) for (int j = 0; j < cells; ++j)
    {
        int q[6][2] = { {i,j}, {i+1,j}, {i,j+1}, {i+1,j}, {i+1,j+1}, {i,j+1} };
        for (auto& c : q)
        {
            float x = -1 + 2.f * c[0] / cells, y = -1 + 2.f * c[1] / cells;
            pos.insert(pos.end(), { x, y, 0.3f * x * y });
            uv0.insert(uv0.end(), { x, y });
            uv1.insert(uv1.end(), { y, x });
            for (int k = 0; k < 4; ++k) wt.push_back((float) (rand() % kJoints) + 0.3f);
        }
    }
    GLuint vao, vbo[4];
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(4, vbo);
    const std::vector<float>* arrs[4] = { &pos, &uv0, &uv1, &wt };
    int slots[4] = { 0, 2, 3, 10 }, comps[4] = { 3, 2, 2, 4 };
    for (int i = 0; i < 4; ++i)
    {
        glBindBuffer(GL_ARRAY_BUFFER, vbo[i]);
        glBufferData(GL_ARRAY_BUFFER, arrs[i]->size() * 4, arrs[i]->data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(slots[i]);
        glVertexAttribPointer(slots[i], comps[i], GL_FLOAT, 0, comps[i] * 4, nullptr);
    }
    int nverts = (int) (pos.size() / 3);
    GLuint tex2, fbo;
    glGenTextures(1, &tex2);
    glBindTexture(GL_TEXTURE_2D, tex2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 256, 256, 0, GL_RGBA, GL_FLOAT, nullptr);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex2, 0);
    glViewport(0, 0, 256, 256);
    GLuint q;
    glGenQueries(1, &q);
    printf("mesh %d vertices (%d triangles); %d draws per frame; %d rounds\n\n", nverts, nverts / 3, draws, rounds);

    struct Sample { double cpuFrame, frameTotal, gpu; };
    // CPU cost of one palette upload alone: the put, plus the buffer update at the flush
    auto uploadOnly = [&](Prog& g, int count)
    {
        const int N = 20000;
        glUseProgram(g.p);
        double t0 = now();
        for (int i = 0; i < N; ++i) { if (g.block) { putBlock(g, i & 7, count); flush(g); } else putLoose(g, i & 7, count); }
        double t1 = now();
        glFinish();
        return (t1 - t0) / N;
    };
    auto frame = [&](Prog& g, int count, bool upload)
    {
        glUseProgram(g.p);
        glBindVertexArray(vao);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glBeginQuery(GL_TIME_ELAPSED, q);
        double t0 = now();
        for (int d = 0; d < draws; ++d)
        {
            if (upload) { if (g.block) { putBlock(g, d & 7, count); flush(g); } else putLoose(g, d & 7, count); }
            glDrawArrays(GL_TRIANGLES, 0, nverts);
        }
        double t1 = now();
        glEndQuery(GL_TIME_ELAPSED);
        glFinish();
        double t2 = now();
        GLuint64 ns = 0;
        glGetQueryObjectui64v(q, GL_QUERY_RESULT, &ns);
        return Sample { t1 - t0, t2 - t0, ns / 1000.0 };
    };
    auto med = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };

    for (int count : { kJoints, 40 })
    {
        printf("== palette of %d joints (%d bytes) ==\n", count, count * 48);
        std::vector<double> up[2], cpu[2], tot[2], gpu[2], cpu0[2], tot0[2], gpu0[2];
        for (int warm = 0; warm < 2; ++warm) for (Prog& g : progs) { frame(g, count, true); uploadOnly(g, count); }
        for (int r = 0; r < rounds; ++r) for (int a = 0; a < 2; ++a)
        {
            int i = (r & 1) ? 1 - a : a; // interleaved, order flipped every round
            Prog& g = progs[i];
            up[i].push_back(uploadOnly(g, count));
            Sample s = frame(g, count, true);
            cpu[i].push_back(s.cpuFrame); tot[i].push_back(s.frameTotal); gpu[i].push_back(s.gpu);
            Sample z = frame(g, count, false);
            cpu0[i].push_back(z.cpuFrame); tot0[i].push_back(z.frameTotal); gpu0[i].push_back(z.gpu);
        }
        printf("%-28s %10s %14s %14s %10s %15s %15s %10s\n", "median", "upload us", "frame CPU ms", "frame wall ms", "GPU ms", "CPU ms no-up", "wall ms no-up", "GPU no-up");
        for (int i = 0; i < 2; ++i)
            printf("%-28s %10.3f %14.3f %14.3f %10.3f %15.3f %15.3f %10.3f\n", progs[i].label, med(up[i]), med(cpu[i]) / 1000, med(tot[i]) / 1000, med(gpu[i]) / 1000, med(cpu0[i]) / 1000, med(tot0[i]) / 1000, med(gpu0[i]) / 1000);
        printf("per draw, upload + draw CPU submit (us): A %.3f  B %.3f;  draw alone: A %.3f  B %.3f\n\n", med(cpu[0]) / draws, med(cpu[1]) / draws, med(cpu0[0]) / draws, med(cpu0[1]) / draws);
    }
    return 0;
}
