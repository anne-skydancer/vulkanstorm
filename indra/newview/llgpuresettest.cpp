// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// The forced GPU reset test hook. Compiled only with -DVKS_GPU_RESET_TEST=ON (development builds; never on release
// or in CI). See doc/gl/gpu_reset.md, "Testing".
//
// RenderGPUResetTest (debug setting, not persisted), read once a frame, then set back to 0:
//   1  hang the GPU: one long draw, so the driver resets the GPU and the reset is this context's (guilty)
//   2  simulate a guilty reset, 3 an innocent one, 4 an unknown one, without touching the GPU
// Either way the next reset check of the same frame (or of a later one, once the driver has reset the GPU) sees it,
// and the viewer shows the GPU reset message and closes, as it does for a real reset.

#include "llviewerprecompiledheaders.h"
#include "llgpuresettest.h"

#if VKS_GPU_RESET_TEST

#include "llgl.h"
#include "llglheaders.h"
#include "llviewercontrol.h"

namespace
{
    GLuint compile_shader(GLenum type, const char* source)
    {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            char log[1024] = {};
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            LL_WARNS("GPUResetTest") << "The test shader did not compile: " << log << LL_ENDL;
            glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    // One full-screen triangle into an 8 x 8 viewport with a fragment shader that runs 2^31 dependent steps per pixel.
    // Each pixel is one thread, so the draw takes about as long as one thread's loop, some 10 to 30 s on any GPU: far
    // past the 2 s Windows timeout (TDR) and the usual Linux job timeouts (amdgpu 10 s). A draw that long is not
    // preempted either (graphics preemption waits for the pixels in flight). The loop count is a uniform, so the
    // compiler cannot fold the loop, and the result is written, so it cannot drop it. The loop is finite: where the
    // driver has no timeout (Linux NVIDIA) the screen stalls until the draw ends and nothing is reset.
    // The GL state touched is read first and put back, so the viewer's own state caches stay right when no reset
    // follows.
    void hang_gpu()
    {
        static const char* vertex_source =
            "#version 330 core\n"
            "void main()\n"
            "{\n"
            "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
            "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
            "}\n";
        static const char* fragment_source =
            "#version 330 core\n"
            "uniform uint steps;\n"
            "out vec4 color;\n"
            "void main()\n"
            "{\n"
            "    float v = gl_FragCoord.x * 0.001;\n"
            "    for (uint i = 0u; i < steps; ++i)\n"
            "    {\n"
            "        v = fract(v * 1.0001 + 0.3);\n"
            "    }\n"
            "    color = vec4(v);\n"
            "}\n";

        GLuint vs = compile_shader(GL_VERTEX_SHADER, vertex_source);
        GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fragment_source);
        if (!vs || !fs)
        {
            glDeleteShader(vs);
            glDeleteShader(fs);
            return;
        }
        GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glLinkProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked)
        {
            LL_WARNS("GPUResetTest") << "The test program did not link" << LL_ENDL;
            glDeleteProgram(program);
            return;
        }

        // what the draw changes
        GLint viewport[4] = {};
        glGetIntegerv(GL_VIEWPORT, viewport);
        GLint draw_fbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw_fbo);
        GLint vao = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        GLint current_program = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &current_program);
        GLboolean color_mask[4] = { GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE };
        glGetBooleanv(GL_COLOR_WRITEMASK, color_mask);
        const GLenum caps[] = { GL_DEPTH_TEST, GL_STENCIL_TEST, GL_BLEND, GL_CULL_FACE, GL_SCISSOR_TEST, GL_RASTERIZER_DISCARD };
        GLboolean cap_on[sizeof(caps) / sizeof(caps[0])] = {};
        for (size_t i = 0; i < sizeof(caps) / sizeof(caps[0]); ++i)
        {
            cap_on[i] = glIsEnabled(caps[i]);
            glDisable(caps[i]); // nothing may reject the pixels before the shader runs
        }

        GLuint test_vao = 0;
        glGenVertexArrays(1, &test_vao);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glViewport(0, 0, 8, 8);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glUseProgram(program);
        glUniform1ui(glGetUniformLocation(program, "steps"), 0x80000000u);
        glBindVertexArray(test_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFlush(); // submit it now

        LL_WARNS("GPUResetTest") << "RenderGPUResetTest 1: a draw of 10 to 30 s was submitted. With GPU reset notification "
            << (gGLManager.mHasResetNotification ? "on" : "OFF (nothing will be reported)")
            << ", the driver's reset is expected within a few seconds (Windows: about 2 s)." << LL_ENDL;

        // put back what was changed (on a lost context these calls do nothing)
        glBindVertexArray(vao);
        glUseProgram(current_program);
        glColorMask(color_mask[0], color_mask[1], color_mask[2], color_mask[3]);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_fbo);
        for (size_t i = 0; i < sizeof(caps) / sizeof(caps[0]); ++i)
        {
            if (cap_on[i])
            {
                glEnable(caps[i]);
            }
        }
        glDeleteVertexArrays(1, &test_vao);
        glDeleteProgram(program); // deleted once the draw is done with it
    }
}

void LLGPUResetTest::update()
{
    static LLCachedControl<S32> test(gSavedSettings, "RenderGPUResetTest", 0);
    const S32 mode = test;
    if (mode == 0)
    {
        return;
    }
    gSavedSettings.setS32("RenderGPUResetTest", 0); // one test per request

    switch (mode)
    {
        case 1:
            hang_gpu();
            break;
        case 2:
            LL_WARNS("GPUResetTest") << "RenderGPUResetTest 2: simulating a guilty reset" << LL_ENDL;
            gGLManager.noteGraphicsReset(GL_GUILTY_CONTEXT_RESET);
            break;
        case 3:
            LL_WARNS("GPUResetTest") << "RenderGPUResetTest 3: simulating an innocent reset" << LL_ENDL;
            gGLManager.noteGraphicsReset(GL_INNOCENT_CONTEXT_RESET);
            break;
        case 4:
            LL_WARNS("GPUResetTest") << "RenderGPUResetTest 4: simulating an unknown reset" << LL_ENDL;
            gGLManager.noteGraphicsReset(GL_UNKNOWN_CONTEXT_RESET);
            break;
        default:
            LL_WARNS("GPUResetTest") << "RenderGPUResetTest " << mode
                << " is not a test: 1 hangs the GPU, 2 to 4 simulate a guilty, innocent or unknown reset" << LL_ENDL;
            break;
    }
}

#endif // VKS_GPU_RESET_TEST
