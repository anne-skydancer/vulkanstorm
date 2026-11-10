// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
// Multi-core/SIMD study bench (doc/perf/multicore-simd-study.md): fork-join dispatch and wake cost,
// atomic vs non-atomic refcount, and the viewer's SSE affine transform against bit-exact AVX2 and FMA variants.
// Standalone; not part of the viewer build. Build (x64 Native Tools prompt):
//   cl /nologo /O2 /Oi /arch:AVX2 /fp:precise /std:c++20 /EHsc bench_multicore_simd.cpp
#include <immintrin.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
#include <random>

using clk = std::chrono::steady_clock;
static double ns_since(clk::time_point t) { return std::chrono::duration<double, std::nano>(clk::now() - t).count(); }

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
        for (int i = 0; i < n; ++i) th.emplace_back([this] { worker(); });
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

static std::atomic<uint64_t> sink{0};
static void tiny(int c, void*) { volatile int x = c; (void)x; }

static void bench_pool() {
    for (int helpers : {1, 3, 5, 7}) {
        for (int spin : {0, 50}) {
            Pool p(helpers, spin);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            // back-to-back dispatches (passes inside one frame)
            const int N = 2000;
            std::vector<double> v; v.reserve(N);
            for (int i = 0; i < N; ++i) { auto t = clk::now(); p.dispatch(64, tiny, nullptr); v.push_back(ns_since(t)); }
            std::sort(v.begin(), v.end());
            // dispatch after an idle gap (first pass of a frame after GPU wait/swap)
            std::vector<double> w;
            for (int i = 0; i < 200; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(2)); auto t = clk::now(); p.dispatch(64, tiny, nullptr); w.push_back(ns_since(t)); }
            std::sort(w.begin(), w.end());
            printf("pool helpers=%d spin=%dus  back2back median %.1f us p99 %.1f us | after 2ms idle median %.1f us p99 %.1f us\n",
                   helpers, spin, v[N / 2] / 1000, v[N * 99 / 100] / 1000, w[100] / 1000, w[198] / 1000);
        }
    }
}

// ---------------- refcount ----------------
struct RC { mutable int r = 1; char pad[60]; };
struct ARC { mutable std::atomic<int> r{1}; char pad[60]; };
static void bench_refcount() {
    const int M = 1 << 16;  // 64k objects: 4 MB, fits L3 (96 MB) not L2
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
        // hot single object (best case for both)
        t = clk::now();
        for (int k = 0; k < 10000000; ++k) { a[0].r++; if (--a[0].r == 0) sink++; }
        double ha = ns_since(t) / 1e7;
        t = clk::now();
        for (int k = 0; k < 10000000; ++k) { b[0].r.fetch_add(1, std::memory_order_relaxed); if (b[0].r.fetch_sub(1, std::memory_order_acq_rel) == 1) sink++; }
        double hb = ns_since(t) / 1e7;
        printf("refcount ref+unref pair: random 64k objs non-atomic %.2f ns, atomic %.2f ns | hot obj non-atomic %.2f ns, atomic %.2f ns\n", na, nb, ha, hb);
    }
}

// ---------------- affine transform: SSE (viewer order) vs AVX2 2-vertex vs AVX2+FMA ----------------
struct alignas(32) M4 { __m128 r[4]; };
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
static void bench_xf() {
    for (int n : {1024, 65536, 1 << 20}) {
        std::vector<float, std::allocator<float>> raw(4 * n + 16), o1(4 * n + 16), o2(4 * n + 16), o3(4 * n + 16);
        float* src = (float*)(((uintptr_t)raw.data() + 31) & ~31ull);
        float* d1 = (float*)(((uintptr_t)o1.data() + 31) & ~31ull);
        float* d2 = (float*)(((uintptr_t)o2.data() + 31) & ~31ull);
        float* d3 = (float*)(((uintptr_t)o3.data() + 31) & ~31ull);
        std::mt19937 g(7); std::uniform_real_distribution<float> U(-256.f, 256.f);
        for (int i = 0; i < 4 * n; ++i) src[i] = (i % 4 == 3) ? 1.f : U(g);
        M4 m; float mm[16]; for (float& f : mm) f = U(g) / 256.f; mm[15] = 1.f;
        for (int r = 0; r < 4; ++r) m.r[r] = _mm_loadu_ps(mm + 4 * r);
        int reps = std::max(1, (1 << 24) / n);
        auto t = clk::now(); for (int k = 0; k < reps; ++k) xf_sse(m, src, d1, n); double a = ns_since(t) / (double(reps) * n);
        t = clk::now(); for (int k = 0; k < reps; ++k) xf_avx2(m, src, d2, n); double b = ns_since(t) / (double(reps) * n);
        t = clk::now(); for (int k = 0; k < reps; ++k) xf_fma(m, src, d3, n); double c = ns_since(t) / (double(reps) * n);
        size_t diff_avx = 0, diff_fma = 0; uint32_t maxulp = 0;
        for (int i = 0; i < 4 * n; ++i) {
            uint32_t u1, u2, u3; memcpy(&u1, d1 + i, 4); memcpy(&u2, d2 + i, 4); memcpy(&u3, d3 + i, 4);
            diff_avx += (u1 != u2); if (u1 != u3) { diff_fma++; uint32_t d = u1 > u3 ? u1 - u3 : u3 - u1; maxulp = std::max(maxulp, d); }
        }
        printf("affine n=%7d (%.1f MB in): SSE %.3f ns/v, AVX2 %.3f ns/v (x%.2f, %zu floats differ), FMA %.3f ns/v (%zu of %d floats differ, max %u ulp)\n",
               n, 16.0 * n / 1e6, a, b, a / b, diff_avx, c, diff_fma, 4 * n, maxulp);
    }
}

int main(int argc, char** argv) {
    printf("hardware_concurrency=%u\n", std::thread::hardware_concurrency());
    bench_xf();
    bench_refcount();
    bench_pool();
    printf("sink %llu\n", (unsigned long long)sink.load());
}
