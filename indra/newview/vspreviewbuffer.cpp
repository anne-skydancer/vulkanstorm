// See vspreviewbuffer.h. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vspreviewbuffer.h"
#include <limits>
#include <stdexcept>
VSPreviewBuffer::VSPreviewBuffer(U32 mask, bool native) : mMask(mask)
{
    if (!native)
        mGL = new LLVertexBuffer(mask);
}
bool VSPreviewBuffer::allocateBuffer(U32 vertices, U32 indices)
{
    if (mGL)
    {
        const bool ok = mGL->allocateBuffer(vertices, indices);
        if (ok)
        {
            mVertices = vertices;
            mIndices  = indices;
        }
        return ok;
    }
    std::size_t bytes = 0;
    for (U32 type = 0; type < LLVertexBuffer::TYPE_MAX; ++type)
    {
        mOffsets[type] = bytes;
        if (mMask & (1u << type))
        {
            const auto size = std::size_t(vertices) * LLVertexBuffer::sTypeSize[type];
            if (size > std::numeric_limits<U32>::max() - 16 || bytes > std::numeric_limits<U32>::max() - size - 16)
                return false;
            bytes += (size + 15) & ~std::size_t(15);
        }
    }
    try
    {
        mData.resize(bytes / 16);
        mIndex.resize(indices);
    }
    catch (const std::bad_alloc&)
    {
        mData.clear();
        mIndex.clear();
        mVertices = mIndices = 0;
        return false;
    }
    mVertices = vertices;
    mIndices  = indices;
    return true;
}
#define VS_STRIDER(method, T, type)                                       \
    bool VSPreviewBuffer::method(LLStrider<T>& out, U32 first, S32 count) \
    { return mGL ? mGL->method(out, first, count) : strider(out, LLVertexBuffer::type, first, count); }
VS_STRIDER(getVertexStrider, LLVector3, TYPE_VERTEX)
VS_STRIDER(getNormalStrider, LLVector3, TYPE_NORMAL)
VS_STRIDER(getTexCoord0Strider, LLVector2, TYPE_TEXCOORD0)
VS_STRIDER(getWeight4Strider, LLVector4a, TYPE_WEIGHT4)
#undef VS_STRIDER
bool VSPreviewBuffer::getIndexStrider(LLStrider<U16>& out, U32 first, S32 count)
{
    if (mGL)
        return mGL->getIndexStrider(out, first, count);
    if (first > mIndices || (count >= 0 && U32(count) > mIndices - first))
        return false;
    out = mIndex.data() + first;
    out.setStride(sizeof(U16));
    return true;
}
bool VSPreviewBuffer::validateRange(U32 start, U32 end, U32 count, U32 offset) const
{
    if (mGL)
        return mGL->validateRange(start, end, count, offset);
    if (start > end || end >= mVertices || offset > mIndices || count > mIndices - offset)
        return false;
    for (U32 i = offset; i < offset + count; ++i)
        if (mIndex[i] < start || mIndex[i] > end)
            return false;
    return true;
}
void VSPreviewBuffer::drawRange(U32 mode, U32 start, U32 end, U32 count, U32 offset) const
{
    if (mGL)
    {
        mGL->drawRange(mode, start, end, count, offset);
        return;
    }
    if (!validateRange(start, end, count, offset))
        throw std::runtime_error("Invalid model preview index range");
    // Native rendering consumes these CPU attributes in VSScenePreview; it
    // must never enter the reference buffer's immediate OpenGL draw path.
    throw std::logic_error("Native model draws require the scene preview renderer");
}
void VSPreviewBuffer::draw(U32 mode, U32 count, U32 offset) const
{
    if (mGL)
    {
        mGL->draw(mode, count, offset);
        return;
    }
    drawRange(mode, 0, mVertices ? mVertices - 1 : 0, count, offset);
}
const F32* VSPreviewBuffer::attribute(LLVertexBuffer::AttributeType type, U32 vertex) const
{
    if (mGL || !hasDataType(type) || vertex >= mVertices)
        return nullptr;
    return reinterpret_cast<const F32*>(reinterpret_cast<const U8*>(mData.data()) + mOffsets[type] +
                                        std::size_t(vertex) * LLVertexBuffer::sTypeSize[type]);
}
