// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "llgl.h"
#include "llimagegl.h"
#include "llpointer.h"
#include <deque>

// A bindless snapshot, never a handle to the mutable viewer texture itself.
// Render-thread ownership; release before destroying the GL context.
class LLParticleTexture
{
public:
    LLParticleTexture() = default;
    ~LLParticleTexture();
    LLParticleTexture(const LLParticleTexture&) = delete;
    LLParticleTexture& operator=(const LLParticleTexture&) = delete;
    bool update(LLImageGL* image);
    GLuint64 handle() const { return mHandle; }
    void release();
    static void collect(bool wait = false); // wait only at GL context teardown
private:
    struct Retired { GLuint texture; GLuint64 handle; GLsync fence; };
    void retire();
    LLPointer<LLImageGL> mImage;
    GLuint mSource = 0, mTexture = 0;
    GLuint64 mHandle = 0;
    U64 mRevision = 0;
    GLint mWidth = 0, mHeight = 0, mLevels = 0, mFormat = 0;
    GLint mParameters[10] = {};
    GLfloat mFloatParameters[8] = {};
    static std::deque<Retired> sRetired;
};
