// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
// Multi-core/SIMD study bench (doc/perf/multicore-simd-study.md). Standalone; not part of the viewer build.
//
// Sections (all by default, or name them on the command line):
//   xf       the viewer's SSE affine transform (LLMatrix4a::affineTransformSSE, llmatrix4a.h:194) against a
//            bit-exact AVX2 version (two vertices per register) and an FMA version
//   rot      the normal/tangent rotation (LLMatrix4a::rotate, llmatrix4a.h:178), SSE against bit-exact AVX2
//   aabb     the octree's box-against-frustum test (LLCamera::AABBInFrustum, llcamera.cpp:207) one box at a time
//            against eight boxes per register (structure of arrays), decisions compared
//   refcount non-atomic against atomic reference counting (LLRefCount, llrefcount.h:81)
//   pool     fork-join dispatch and wake cost (main thread takes part, helpers spin then park)
//   sleep    Sleep(1) granularity, the wake floor of an idle LL::ThreadPool worker (threadpool.cpp:36-58)
//   scale    parallel_for scaling of a geometry-fill-like job and a cull-like job with 0..7 helpers
//
// Build (x64 Native Tools prompt), the viewer's Windows code-generation flags:
//   cl /nologo /O2 /Oi /arch:AVX2 /fp:precise /std:c++20 /EHsc bench_multicore_simd.cpp winmm.lib
#include <immintrin.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <timeapi.h>
#endif

using clk = std::chrono::steady_clock;
static double ns_since(clk::time_point t) { return std::chrono::duration<double, std::nano>(clk::now() - t).count(); }

// The viewer's main thread runs with flush-to-zero and round-to-nearest (LLVector4a::initClass, llvector4a.h:63-66).
// MXCSR is per thread: every thread of the bench sets the same, as a FrameJobs helper must.
static void init_fpu() { _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON); _MM_SET_ROUNDING_MODE(_MM_ROUND_NEAREST); }

static std::atomic<uint64_t> sink{0};
static void* amalloc(size_t n) { return _mm_malloc(n, 64); }

// ---------------- fork-join pool (main participates), spin-then-park ----------------
struct Pool {
    std::vector<std::thread> th;
    std::atomic<uint32_t> gen{0};
    std::atomic<int> next{0}, done{0};
    int nchunks = 0;
    void (*fn)(int, void*) = nullptr;
    void* arg = nullptr;
    std::atomic<bool> quit{false};
    int spin_us;
    explicit Pool(int n, int spin) : spin_us(spin) {
        for (int i = 0; i < n; ++i) th.emplace_back([this] { init_fpu(); worker(); });
    }
    ~Pool() { quit = true; gen.fetch_add(1); gen.notify_all(); for (auto& t : th) t.join(); }
    void run_chunks() {
        int c;
        while ((c = next.fetch_add(1, std::memory_order_relaxed)) < nchunks) { fn(c, arg); done.fetch_add(1, std::memory_order_release); }
    }
    void worker() {
        uint32_t seen = gen.load();
        for (;;) {
            auto t0 = clk::now();
            uint32_t g;
            while ((g = gen.load(std::memory_order_acquire)) == seen) {
                if (ns_since(t0) > spin_us * 1000.0) { gen.wait(seen); }
                else _mm_pause();
            }
            seen = g;
            if (quit) return;
            run_chunks();
        }
    }
    void dispatch(int chunks, void (*f)(int, void*), void* a) {
        fn = f; arg = a; nchunks = chunks; done.store(0); next.store(0);
        gen.fetch_add(1, std::memory_order_release);
        gen.notify_all();
        run_chunks();
        while (done.load(std::memory_order_acquire) < chunks) _mm_pause();
    }
};

static void tiny(int c, void*) { volatile int x = c; (void)x; }

static void bench_pool() {
    for (int helpers : {1, 3, 5, 7}) {
        for (int spin : {0, 50}) {
            Pool p(helpers, spin);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            const int N = 2000;
            std::vector<double> v; v.reserve(N);
            for (int i = 0; i < N; ++i) { auto t = clk::now(); p.dispatch(64, tiny, nullptr); v.push_back(ns_since(t)); }
            std::sort(v.begin(), v.end());
            std::vector<double> w;
            for (int i = 0; i < 200; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); auto t = clk::now(); p.dispatch(64, tiny, nullptr); w.push_back(ns_since(t)); }
            std::sort(w.begin(), w.end());
            printf("pool helpers=%d spin=%dus  back2back median %.1f us p99 %.1f us | after 2ms idle median %.1f us p99 %.1f us\n",
                   helpers, spin, v[N / 2] / 1000, v[N * 99 / 100] / 1000, w[100] / 1000, w[198] / 1000);
        }
    }
}

// ---------------- Sleep(1): the floor of an idle LL::ThreadPool worker's wake ----------------
static void bench_sleep() {
#if defined(_WIN32)
    for (int period : {0, 1}) {
        if (period) timeBeginPeriod(1);
        std::vector<double> v;
        for (int i = 0; i < 200; ++i) { auto t = clk::now(); Sleep(1); v.push_back(ns_since(t)); }
        std::sort(v.begin(), v.end());
        printf("Sleep(1) %s: median %.3f ms p99 %.3f ms min %.3f ms\n", period ? "after timeBeginPeriod(1)" : "default timer      ",
               v[100] / 1e6, v[198] / 1e6, v[0] / 1e6);
        if (period) timeEndPeriod(1);
    }
#else
    printf("sleep: Windows only\n");
#endif
}

// ---------------- refcount ----------------
struct RC { mutable int r = 1; char pad[60]; };
struct ARC { mutable std::atomic<int> r{1}; char pad[60]; };
static void bench_refcount() {
    const int M = 1 << 16;
    std::vector<RC> a(M); std::vector<ARC> b(M);
    std::vector<int> idx(M); for (int i = 0; i < M; ++i) idx[i] = i;
    std::shuffle(idx.begin(), idx.end(), std::mt19937(1));
    for (int rep = 0; rep < 2; ++rep) {
        auto t = clk::now();
        for (int k = 0; k < 50; ++k) for (int i : idx) { a[i].r++; if (--a[i].r == 0) sink++; }
        double na = ns_since(t) / (50.0 * M);
        t = clk::now();
        for (int k = 0; k < 50; ++k) for (int i : idx) { b[i].r.fetch_add(1, std::memory_order_relaxed); if (b[i].r.fetch_sub(1, std::memory_order_acq_rel) == 1) sink++; }
        double nb = ns_since(t) / (50.0 * M);
        t = clk::now();
        for (int k = 0; k < 10000000; ++k) { a[0].r++; if (--a[0].r == 0) sink++; }
        double ha = ns_since(t) / 1e7;
        t = clk::now();
        for (int k = 0; k < 10000000; ++k) { b[0].r.fetch_add(1, std::memory_order_relaxed); if (b[0].r.fetch_sub(1, std::memory_order_acq_rel) == 1) sink++; }
        double hb = ns_since(t) / 1e7;
        printf("refcount ref+unref pair: random 64k objs non-atomic %.2f ns, atomic %.2f ns | hot obj non-atomic %.2f ns, atomic %.2f ns\n", na, nb, ha, hb);
    }
}

// ---------------- K1 affine transform and K2 rotation ----------------
struct alignas(32) M4 { __m128 r[4]; };
// K1, the viewer's order: (x*r0 + y*r1) + (z*r2 + r3)
static void xf_sse(const M4& m, const float* src, float* dst, int n) {
    for (int i = 0; i < n; ++i) {
        __m128 v = _mm_load_ps(src + 4 * i);
        __m128 x = _mm_shuffle_ps(v, v, 0x00), y = _mm_shuffle_ps(v, v, 0x55), z = _mm_shuffle_ps(v, v, 0xAA);
        x = _mm_mul_ps(x, m.r[0]); y = _mm_mul_ps(y, m.r[1]); z = _mm_mul_ps(z, m.r[2]);
        x = _mm_add_ps(x, y); z = _mm_add_ps(z, m.r[3]);
        _mm_store_ps(dst + 4 * i, _mm_add_ps(x, z));
    }
}
static void xf_avx2(const M4& m, const float* src, float* dst, int n) {
    __m256 r0 = _mm256_broadcast_ps(&m.r[0]), r1 = _mm256_broadcast_ps(&m.r[1]), r2 = _mm256_broadcast_ps(&m.r[2]), r3 = _mm256_broadcast_ps(&m.r[3]);
    int i = 0;
    for (; i + 2 <= n; i += 2) {
        __m256 v = _mm256_load_ps(src + 4 * i);
        __m256 x = _mm256_permute_ps(v, 0x00), y = _mm256_permute_ps(v, 0x55), z = _mm256_permute_ps(v, 0xAA);
        x = _mm256_mul_ps(x, r0); y = _mm256_mul_ps(y, r1); z = _mm256_mul_ps(z, r2);
        x = _mm256_add_ps(x, y); z = _mm256_add_ps(z, r3);
        _mm256_store_ps(dst + 4 * i, _mm256_add_ps(x, z));
    }
    if (i < n) xf_sse(m, src + 4 * i, dst + 4 * i, n - i);
}
static void xf_fma(const M4& m, const float* src, float* dst, int n) {
    __m256 r0 = _mm256_broadcast_ps(&m.r[0]), r1 = _mm256_broadcast_ps(&m.r[1]), r2 = _mm256_broadcast_ps(&m.r[2]), r3 = _mm256_broadcast_ps(&m.r[3]);
    for (int i = 0; i + 2 <= n; i += 2) {
        __m256 v = _mm256_load_ps(src + 4 * i);
        __m256 x = _mm256_permute_ps(v, 0x00), y = _mm256_permute_ps(v, 0x55), z = _mm256_permute_ps(v, 0xAA);
        __m256 acc = _mm256_fmadd_ps(z, r2, r3);
        acc = _mm256_fmadd_ps(y, r1, acc);
        acc = _mm256_fmadd_ps(x, r0, acc);
        _mm256_store_ps(dst + 4 * i, acc);
    }
}
// K2, the viewer's order: (x*r0 + y*r1) + z*r2
static void rot_sse(const M4& m, const float* src, float* dst, int n) {
    for (int i = 0; i < n; ++i) {
        __m128 v = _mm_load_ps(src + 4 * i);
        __m128 x = _mm_shuffle_ps(v, v, 0x00), y = _mm_shuffle_ps(v, v, 0x55), z = _mm_shuffle_ps(v, v, 0xAA);
        x = _mm_mul_ps(x, m.r[0]); y = _mm_mul_ps(y, m.r[1]); z = _mm_mul_ps(z, m.r[2]);
        x = _mm_add_ps(x, y);
        _mm_store_ps(dst + 4 * i, _mm_add_ps(x, z));
    }
}
static void rot_avx2(const M4& m, const float* src, float* dst, int n) {
    __m256 r0 = _mm256_broadcast_ps(&m.r[0]), r1 = _mm256_broadcast_ps(&m.r[1]), r2 = _mm256_broadcast_ps(&m.r[2]);
    int i = 0;
    for (; i + 2 <= n; i += 2) {
        __m256 v = _mm256_load_ps(src + 4 * i);
        __m256 x = _mm256_permute_ps(v, 0x00), y = _mm256_permute_ps(v, 0x55), z = _mm256_permute_ps(v, 0xAA);
        x = _mm256_mul_ps(x, r0); y = _mm256_mul_ps(y, r1); z = _mm256_mul_ps(z, r2);
        x = _mm256_add_ps(x, y);
        _mm256_store_ps(dst + 4 * i, _mm256_add_ps(x, z));
    }
    if (i < n) rot_sse(m, src + 4 * i, dst + 4 * i, n - i);
}

static size_t count_diff(const float* a, const float* b, size_t n, uint32_t* maxulp) {
    size_t d = 0; uint32_t mu = 0;
    for (size_t i = 0; i < n; ++i) {
        uint32_t u1, u2; memcpy(&u1, a + i, 4); memcpy(&u2, b + i, 4);
        if (u1 != u2) { ++d; uint32_t x = u1 > u2 ? u1 - u2 : u2 - u1; mu = std::max(mu, x); }
    }
    if (maxulp) *maxulp = mu;
    return d;
}

// Inputs with the edge cases a bit-for-bit test must cover: denormals, +-0, +-inf, NaN, huge and tiny values.
static void fill_vertices(float* src, int n, uint32_t seed) {
    std::mt19937 g(seed); std::uniform_real_distribution<float> U(-256.f, 256.f);
    const float special[] = {0.f, -0.f, 1e-40f, -1e-40f, INFINITY, -INFINITY, NAN, 3.0e38f, -3.0e38f, 1e-30f};
    for (int i = 0; i < 4 * n; ++i) src[i] = (i % 4 == 3) ? 1.f : U(g);
    for (int i = 0; i < 64 && i < n; ++i) src[4 * i + (i % 3)] = special[i % 10];
}

static void bench_xf() {
    for (int n : {1024, 65536, 1 << 20}) {
        float* src = (float*)amalloc(16ull * n); float* d1 = (float*)amalloc(16ull * n);
        float* d2 = (float*)amalloc(16ull * n); float* d3 = (float*)amalloc(16ull * n);
        fill_vertices(src, n, 7);
        std::mt19937 g(9); std::uniform_real_distribution<float> U(-1.f, 1.f);
        M4 m; float mm[16]; for (float& f : mm) f = U(g); mm[15] = 1.f;
        for (int r = 0; r < 4; ++r) m.r[r] = _mm_loadu_ps(mm + 4 * r);
        int reps = std::max(1, (1 << 24) / n);
        double best[3] = {1e30, 1e30, 1e30};
        for (int trial = 0; trial < 3; ++trial) {
            auto t = clk::now(); for (int k = 0; k < reps; ++k) xf_sse(m, src, d1, n); best[0] = std::min(best[0], ns_since(t) / (double(reps) * n));
            t = clk::now(); for (int k = 0; k < reps; ++k) xf_avx2(m, src, d2, n); best[1] = std::min(best[1], ns_since(t) / (double(reps) * n));
            t = clk::now(); for (int k = 0; k < reps; ++k) xf_fma(m, src, d3, n); best[2] = std::min(best[2], ns_since(t) / (double(reps) * n));
        }
        uint32_t mu_a, mu_f;
        size_t da = count_diff(d1, d2, 4ull * n, &mu_a), df = count_diff(d1, d3, 4ull * n, &mu_f);
        printf("affine n=%7d (%.1f MB in): SSE %.3f ns/v, AVX2 %.3f ns/v (x%.2f, %zu floats differ), FMA %.3f ns/v (x%.2f, %zu of %d floats differ)\n",
               n, 16.0 * n / 1e6, best[0], best[1], best[0] / best[1], da, best[2], best[0] / best[2], df, 4 * n);
        // K2
        best[0] = best[1] = 1e30;
        for (int trial = 0; trial < 3; ++trial) {
            auto t = clk::now(); for (int k = 0; k < reps; ++k) rot_sse(m, src, d1, n); best[0] = std::min(best[0], ns_since(t) / (double(reps) * n));
            t = clk::now(); for (int k = 0; k < reps; ++k) rot_avx2(m, src, d2, n); best[1] = std::min(best[1], ns_since(t) / (double(reps) * n));
        }
        da = count_diff(d1, d2, 4ull * n, &mu_a);
        printf("rotate n=%7d (%.1f MB in): SSE %.3f ns/v, AVX2 %.3f ns/v (x%.2f, %zu floats differ)\n",
               n, 16.0 * n / 1e6, best[0], best[1], best[0] / best[1], da);
        _mm_free(src); _mm_free(d1); _mm_free(d2); _mm_free(d3);
    }
}

// ---------------- K5 AABB against frustum planes ----------------
// One box at a time, as LLCamera::AABBInFrustum (llcamera.cpp:207-244): for each plane with a valid octant mask, the box
// corner nearest along the plane normal (center - radius*scaler[mask]) decides "outside" (return 0), the farthest corner
// decides "intersects" (1); else "inside" (2). dot3 is (x*px + y*py) + z*pz (llvector4a.inl:315-326), d is -p.w by sign flip.
struct alignas(16) Plane { float v[4]; uint8_t mask; };
static const float kScaler[8][4] = {{-1,-1,-1,0},{1,-1,-1,0},{-1,1,-1,0},{1,1,-1,0},{-1,-1,1,0},{1,-1,1,0},{-1,1,1,0},{1,1,1,0}};

static int aabb_aos(const float* c, const float* r, const Plane* pl, int np) {
    bool result = false;
    for (int i = 0; i < np; ++i) {
        const Plane& p = pl[i];
        if (p.mask >= 8) continue;
        __m128 P = _mm_load_ps(p.v);
        __m128 rs = _mm_mul_ps(_mm_load_ps(r), _mm_load_ps(kScaler[p.mask]));
        __m128 C = _mm_load_ps(c);
        __m128 minp = _mm_sub_ps(C, rs);
        __m128 ab = _mm_mul_ps(P, minp);
        __m128 dot = _mm_add_ps(_mm_add_ps(ab, _mm_shuffle_ps(ab, ab, 0x55)), _mm_shuffle_ps(ab, ab, 0xAA));
        float d = -p.v[3];
        if (_mm_cvtss_f32(dot) > d) return 0;
        if (!result) {
            __m128 maxp = _mm_add_ps(C, rs);
            __m128 ab2 = _mm_mul_ps(P, maxp);
            __m128 dot2 = _mm_add_ps(_mm_add_ps(ab2, _mm_shuffle_ps(ab2, ab2, 0x55)), _mm_shuffle_ps(ab2, ab2, 0xAA));
            result = _mm_cvtss_f32(dot2) > d;
        }
    }
    return result ? 1 : 2;
}

// Eight boxes per register (structure of arrays): cx[8], cy[8], cz[8], rx[8], ry[8], rz[8]. Same per-lane operations and order.
static void aabb_soa8(const float* cx, const float* cy, const float* cz, const float* rx, const float* ry, const float* rz,
                      const Plane* pl, int np, int* out) {
    __m256 X = _mm256_load_ps(cx), Y = _mm256_load_ps(cy), Z = _mm256_load_ps(cz);
    __m256 RX = _mm256_load_ps(rx), RY = _mm256_load_ps(ry), RZ = _mm256_load_ps(rz);
    __m256 outside = _mm256_setzero_ps(), partial = _mm256_setzero_ps();
    for (int i = 0; i < np; ++i) {
        const Plane& p = pl[i];
        if (p.mask >= 8) continue;
        const float* s = kScaler[p.mask];
        __m256 px = _mm256_set1_ps(p.v[0]), py = _mm256_set1_ps(p.v[1]), pz = _mm256_set1_ps(p.v[2]);
        __m256 d = _mm256_set1_ps(-p.v[3]);
        __m256 rsx = _mm256_mul_ps(RX, _mm256_set1_ps(s[0])), rsy = _mm256_mul_ps(RY, _mm256_set1_ps(s[1])), rsz = _mm256_mul_ps(RZ, _mm256_set1_ps(s[2]));
        __m256 mnx = _mm256_sub_ps(X, rsx), mny = _mm256_sub_ps(Y, rsy), mnz = _mm256_sub_ps(Z, rsz);
        __m256 dmin = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(px, mnx), _mm256_mul_ps(py, mny)), _mm256_mul_ps(pz, mnz));
        outside = _mm256_or_ps(outside, _mm256_cmp_ps(dmin, d, _CMP_GT_OQ));
        __m256 mxx = _mm256_add_ps(X, rsx), mxy = _mm256_add_ps(Y, rsy), mxz = _mm256_add_ps(Z, rsz);
        __m256 dmax = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(px, mxx), _mm256_mul_ps(py, mxy)), _mm256_mul_ps(pz, mxz));
        partial = _mm256_or_ps(partial, _mm256_cmp_ps(dmax, d, _CMP_GT_OQ));
    }
    int mo = _mm256_movemask_ps(outside), mp = _mm256_movemask_ps(partial);
    for (int k = 0; k < 8; ++k) out[k] = (mo >> k & 1) ? 0 : ((mp >> k & 1) ? 1 : 2);
}

static void make_planes(Plane* pl, std::mt19937& g) {
    // a perspective frustum looking down +x from the origin, 60 degrees, near 0.5, far 256: 6 planes, normals inward
    // (LLCamera keeps planes whose dot with an inside point is negative: n.p + d < 0 inside; we use the same convention)
    const float a = 0.5f;  // tan(30 deg) approx: the side planes' slope
    float n[6][4] = {{-1, 0, 0, 0.5f}, {1, 0, 0, -256.f}, {-a, 1, 0, 0}, {-a, -1, 0, 0}, {-a, 0, 1, 0}, {-a, 0, -1, 0}};
    std::uniform_real_distribution<float> J(-0.01f, 0.01f);
    for (int i = 0; i < 6; ++i) {
        float len = std::sqrt(n[i][0] * n[i][0] + n[i][1] * n[i][1] + n[i][2] * n[i][2]);
        for (int k = 0; k < 3; ++k) pl[i].v[k] = n[i][k] / len + J(g);
        pl[i].v[3] = n[i][3] / len;
        __m128 v = _mm_load_ps(pl[i].v);
        pl[i].mask = (uint8_t)(_mm_movemask_ps(_mm_cmpge_ps(v, _mm_setzero_ps())) & 7);
    }
}

static void bench_aabb() {
    const int NB = 1 << 16;   // 64k boxes = 8k octree nodes of 8 children
    std::mt19937 g(3);
    std::uniform_real_distribution<float> P(-300.f, 300.f), R(0.1f, 40.f);
    float* aos = (float*)amalloc(32ull * NB);
    float* soa = (float*)amalloc(24ull * NB);
    for (int b = 0; b < NB; ++b) {
        float c[3] = {P(g), P(g), P(g)}, r[3] = {R(g), R(g), R(g)};
        if (b % 997 == 0) { c[0] = 0.f; r[1] = 1e-40f; }      // a denormal radius and a box on the camera
        for (int k = 0; k < 3; ++k) { aos[8 * b + k] = c[k]; aos[8 * b + 4 + k] = r[k]; }
        aos[8 * b + 3] = aos[8 * b + 7] = 0.f;
        int blk = b / 8, lane = b % 8;
        for (int k = 0; k < 3; ++k) { soa[blk * 48 + k * 8 + lane] = c[k]; soa[blk * 48 + 24 + k * 8 + lane] = r[k]; }
    }
    Plane pl[6]; make_planes(pl, g);
    std::vector<int> ra(NB), rb(NB);
    int counts[3] = {0, 0, 0};
    double best_a = 1e30, best_b = 1e30;
    for (int trial = 0; trial < 5; ++trial) {
        auto t = clk::now();
        for (int b = 0; b < NB; ++b) ra[b] = aabb_aos(aos + 8 * b, aos + 8 * b + 4, pl, 6);
        best_a = std::min(best_a, ns_since(t) / NB);
        t = clk::now();
        for (int blk = 0; blk < NB / 8; ++blk) {
            const float* s = soa + blk * 48;
            aabb_soa8(s, s + 8, s + 16, s + 24, s + 32, s + 40, pl, 6, &rb[blk * 8]);
        }
        best_b = std::min(best_b, ns_since(t) / NB);
    }
    size_t diff = 0;
    for (int b = 0; b < NB; ++b) { diff += ra[b] != rb[b]; counts[ra[b]]++; }
    printf("aabb 64k boxes, 6 planes: one box at a time %.2f ns/box, 8 boxes per register %.2f ns/box (x%.2f), %zu decisions differ (out %d / partial %d / in %d)\n",
           best_a, best_b, best_a / best_b, diff, counts[0], counts[1], counts[2]);
    _mm_free(aos); _mm_free(soa);
}

// ---------------- parallel_for scaling on the 8 cores: geometry-fill-like and cull-like jobs ----------------
struct FillJob { const M4* m; const float* src; float* dst; float* nrm; int verts_per_group; };
static void fill_chunk(int c, void* a) {
    FillJob* j = (FillJob*)a;
    size_t off = 4ull * c * j->verts_per_group;
    xf_sse(*j->m, j->src + off, j->dst + off, j->verts_per_group);       // positions (as today, SSE)
    rot_sse(*j->m, j->src + off, j->nrm + off, j->verts_per_group);      // normals
}
struct CullJob { const float* aos; const Plane* pl; int* res; int boxes_per_chunk; };
static void cull_chunk(int c, void* a) {
    CullJob* j = (CullJob*)a;
    int b0 = c * j->boxes_per_chunk;
    for (int b = b0; b < b0 + j->boxes_per_chunk; ++b) j->res[b] = aabb_aos(j->aos + 8 * b, j->aos + 8 * b + 4, j->pl, 6);
}

static double time_dispatch(Pool* p, int chunks, void (*f)(int, void*), void* a, int reps) {
    std::vector<double> v;
    for (int r = 0; r < reps; ++r) {
        auto t = clk::now();
        if (p) p->dispatch(chunks, f, a); else for (int c = 0; c < chunks; ++c) f(c, a);
        v.push_back(ns_since(t));
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2] / 1e6;
}

static void bench_scale() {
    // geometry fill: 512 groups of 1500 vertices (768k vertices, 12 MB in, 24 MB out): the vertex count of a dense
    // region's rebuild burst after a teleport [assumed shape, not a viewer capture]
    const int G = 512, V = 1500;
    float* src = (float*)amalloc(16ull * G * V); float* dst = (float*)amalloc(16ull * G * V); float* nrm = (float*)amalloc(16ull * G * V);
    fill_vertices(src, G * V, 11);
    M4 m; for (int r = 0; r < 4; ++r) m.r[r] = _mm_set_ps(r == 3 ? 1.f : 0.f, 0.3f * r, 0.5f, 0.7f);
    FillJob fj{&m, src, dst, nrm, V};
    // cull: 64k boxes in 256 chunks of 256 (octree subtrees)
    const int NB = 1 << 16, CH = 256;
    float* aos = (float*)amalloc(32ull * NB);
    std::mt19937 g(5); std::uniform_real_distribution<float> P(-300.f, 300.f), R(0.1f, 40.f);
    for (int b = 0; b < NB; ++b) { for (int k = 0; k < 3; ++k) { aos[8 * b + k] = P(g); aos[8 * b + 4 + k] = R(g); } aos[8 * b + 3] = aos[8 * b + 7] = 0.f; }
    Plane pl[6]; make_planes(pl, g);
    std::vector<int> res(NB);
    CullJob cj{aos, pl, res.data(), NB / CH};

    double fill0 = time_dispatch(nullptr, G, fill_chunk, &fj, 15);
    double cull0 = time_dispatch(nullptr, CH, cull_chunk, &cj, 31);
    printf("scale serial: fill %.3f ms (%d groups x %d verts), cull %.3f ms (%d boxes)\n", fill0, G, V, cull0, NB);
    for (int helpers : {1, 2, 3, 4, 5, 6, 7}) {
        Pool p(helpers, 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        double f = time_dispatch(&p, G, fill_chunk, &fj, 15);
        double c = time_dispatch(&p, CH, cull_chunk, &cj, 31);
        printf("scale helpers=%d (+main): fill %.3f ms (x%.2f), cull %.3f ms (x%.2f)\n", helpers, f, fill0 / f, c, cull0 / c);
    }
    _mm_free(src); _mm_free(dst); _mm_free(nrm); _mm_free(aos);
}

int main(int argc, char** argv) {
    init_fpu();
    printf("hardware_concurrency=%u\n", std::thread::hardware_concurrency());
    auto want = [&](const char* s) {
        if (argc < 2) return true;
        for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == s) return true;
        return false;
    };
    if (want("xf") || want("rot")) bench_xf();
    if (want("aabb")) bench_aabb();
    if (want("refcount")) bench_refcount();
    if (want("pool")) bench_pool();
    if (want("sleep")) bench_sleep();
    if (want("scale")) bench_scale();
    printf("sink %llu\n", (unsigned long long)sink.load());
}
