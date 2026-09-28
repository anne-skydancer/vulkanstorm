// SPDX-License-Identifier: LGPL-2.1-or-later
#include "llviewerprecompiledheaders.h"
#include "llparticletexture.h"
#include <algorithm>

namespace
{
constexpr GLenum PARAMETERS[] = { GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER,
    GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T, GL_TEXTURE_BASE_LEVEL, GL_TEXTURE_MAX_LEVEL,
    GL_TEXTURE_SWIZZLE_R, GL_TEXTURE_SWIZZLE_G, GL_TEXTURE_SWIZZLE_B, GL_TEXTURE_SWIZZLE_A };
}
LLParticleTexture::~LLParticleTexture() { release(); }
std::deque<LLParticleTexture::Retired> LLParticleTexture::sRetired;

void LLParticleTexture::collect(bool wait)
{
    if (wait && !sRetired.empty()) glFinish();
    while (!sRetired.empty())
    {
        auto& r = sRetired.front();
        GLenum status = glClientWaitSync(r.fence, 0, 0);
        if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) break;
        if (r.handle) glMakeTextureHandleNonResidentARB(r.handle);
        glDeleteTextures(1, &r.texture);
        glDeleteSync(r.fence);
        sRetired.pop_front();
    }
}
void LLParticleTexture::retire()
{
    if (mTexture)
        sRetired.push_back({mTexture, mHandle, glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0)});
    mTexture = 0;
    mHandle = 0;
}
void LLParticleTexture::release()
{
    retire();
    collect();
    mImage = nullptr;
    mSource = 0;
    mRevision = 0;
}
bool LLParticleTexture::update(LLImageGL* image)
{
    collect();
    if (!image || !image->getTexName())
    {
        LL_WARNS("ParticlePipeline") << "Texture snapshot has no published source image" << LL_ENDL;
        return false;
    }
    const GLuint source = image->getTexName();
    const U64 revision = image->getContentRevision();
    image->updateBindStats();
    if (mImage == image && source == mSource && revision == mRevision && mTexture) return true;

    GLint width = 0, height = 0, format = 0, parameters[10];
    glGetTextureLevelParameteriv(source, 0, GL_TEXTURE_WIDTH, &width);
    glGetTextureLevelParameteriv(source, 0, GL_TEXTURE_HEIGHT, &height);
    glGetTextureLevelParameteriv(source, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
    if (!width || !height)
    {
        LL_WARNS("ParticlePipeline") << "Texture snapshot has no level-zero storage: source=" << source
            << " width=" << width << " height=" << height << " GL error=" << glGetError() << LL_ENDL;
        return false;
    }
    // Legacy unsized storage reports are normalized to equivalent sized formats.
    if (format == GL_RGBA) format = GL_RGBA8;
    if (format == GL_RGB) format = GL_RGB8;
    GLint levels = 1;
    for (GLint w = width, h = height; w > 1 || h > 1; ++levels)
    {
        GLint next = 0;
        glGetTextureLevelParameteriv(source, levels, GL_TEXTURE_WIDTH, &next);
        if (!next) break;
        w = std::max(w/2, 1); h = std::max(h/2, 1);
    }
    for (size_t i = 0; i < std::size(PARAMETERS); ++i)
        glGetTextureParameteriv(source, PARAMETERS[i], &parameters[i]);
    parameters[5] = std::min(parameters[5], levels-1);
    GLfloat floats[8]{};
    glGetTextureParameterfv(source,GL_TEXTURE_MIN_LOD,&floats[0]);
    glGetTextureParameterfv(source,GL_TEXTURE_MAX_LOD,&floats[1]);
    glGetTextureParameterfv(source,GL_TEXTURE_LOD_BIAS,&floats[2]);
    glGetTextureParameterfv(source,GL_TEXTURE_BORDER_COLOR,&floats[3]);
    if (gGLManager.mHasAnisotropic) glGetTextureParameterfv(source,GL_TEXTURE_MAX_ANISOTROPY_EXT,&floats[7]);
    if (!mTexture || width != mWidth || height != mHeight || levels != mLevels ||
        format != mFormat || !std::equal(std::begin(parameters), std::end(parameters), mParameters) ||
        !std::equal(std::begin(floats),std::end(floats),mFloatParameters))
    {
        retire();
        glCreateTextures(GL_TEXTURE_2D, 1, &mTexture);
        glTextureStorage2D(mTexture, levels, format, width, height);
        for (size_t i = 0; i < std::size(PARAMETERS); ++i)
            glTextureParameteri(mTexture, PARAMETERS[i], parameters[i]);
        glTextureParameterf(mTexture,GL_TEXTURE_MIN_LOD,floats[0]);
        glTextureParameterf(mTexture,GL_TEXTURE_MAX_LOD,floats[1]);
        glTextureParameterf(mTexture,GL_TEXTURE_LOD_BIAS,floats[2]);
        glTextureParameterfv(mTexture,GL_TEXTURE_BORDER_COLOR,&floats[3]);
        if (gGLManager.mHasAnisotropic) glTextureParameterf(mTexture,GL_TEXTURE_MAX_ANISOTROPY_EXT,floats[7]);
        std::copy(std::begin(floats),std::end(floats),mFloatParameters);
        mWidth = width; mHeight = height; mLevels = levels; mFormat = format;
        std::copy(std::begin(parameters), std::end(parameters), mParameters);
    }
    // Copy the original format, including compressed/sRGB data and every mip.
    // No decode, readback, or per-frame copy of unchanged textures is involved.
    for (GLint level = 0, w = width, h = height; level < levels; ++level)
    {
        glCopyImageSubData(source, GL_TEXTURE_2D, level, 0, 0, 0,
            mTexture, GL_TEXTURE_2D, level, 0, 0, 0, w, h, 1);
        w = std::max(w/2, 1); h = std::max(h/2, 1);
    }
    glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
    if (!mHandle)
    {
        mHandle = glGetTextureHandleARB(mTexture);
        glMakeTextureHandleResidentARB(mHandle);
    }
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR)
    {
        LL_WARNS("ParticlePipeline") << "Texture snapshot GL error=" << error << " source=" << source
            << " width=" << width << " height=" << height << " levels=" << levels
            << " format=" << format << " handle=" << mHandle << LL_ENDL;
        return false;
    }
    mImage = image; mSource = source; mRevision = revision;
    return true;
}
