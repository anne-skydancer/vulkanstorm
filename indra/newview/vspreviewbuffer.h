// CPU/GL storage peers for the existing model import algorithms. LGPL-2.1.
#pragma once
#include "llvertexbuffer.h"
#include "llstrider.h"
#include "llvector4a.h"
#include <array>
#include <vector>
// Native import/LOD algorithms retain the original planar attribute strides;
// they never allocate an OpenGL name or map an OpenGL buffer.
class VSPreviewBuffer : public LLRefCount
{
public:
    explicit VSPreviewBuffer(U32 mask, bool native);
    bool allocateBuffer(U32 vertices, U32 indices);
    bool hasDataType(LLVertexBuffer::AttributeType type) const { return (mMask & (1u << type)) != 0; }
    U32  getTypeMask() const { return mMask; }
    U32  getNumVerts() const { return mVertices; }
    U32  getNumIndices() const { return mIndices; }
    U32  getSize() const { return mGL ? mGL->getSize() : U32(mData.size() * 16); }
    U32  getIndicesSize() const { return mGL ? mGL->getIndicesSize() : U32(mIndex.size() * 2); }
    U8*  getMappedData() { return mGL ? mGL->getMappedData() : reinterpret_cast<U8*>(mData.data()); }
    U8*  getMappedIndices() { return mGL ? mGL->getMappedIndices() : reinterpret_cast<U8*>(mIndex.data()); }
    bool getVertexStrider(LLStrider<LLVector3>&, U32 = 0, S32 = -1);
    bool getNormalStrider(LLStrider<LLVector3>&, U32 = 0, S32 = -1);
    bool getTexCoord0Strider(LLStrider<LLVector2>&, U32 = 0, S32 = -1);
    bool getWeight4Strider(LLStrider<LLVector4a>&, U32 = 0, S32 = -1);
    bool getIndexStrider(LLStrider<U16>&, U32 = 0, S32 = -1);
    void unmapBuffer()
    {
        if (mGL)
            mGL->unmapBuffer();
    }
    void setBuffer()
    {
        if (mGL)
            mGL->setBuffer();
    }
    bool       validateRange(U32 start, U32 end, U32 count, U32 offset) const;
    void       drawRange(U32 mode, U32 start, U32 end, U32 count, U32 offset) const;
    void       draw(U32 mode, U32 count, U32 offset) const;
    const F32* attribute(LLVertexBuffer::AttributeType type, U32 vertex) const;
    const U16* indices() const { return mIndex.data(); }

private:
    LLPointer<LLVertexBuffer>                         mGL;
    U32                                               mMask = 0, mVertices = 0, mIndices = 0;
    std::array<std::size_t, LLVertexBuffer::TYPE_MAX> mOffsets{};
    std::vector<LLVector4a>                           mData;
    std::vector<U16>                                  mIndex;
    template<class T>
    bool strider(LLStrider<T>& out, LLVertexBuffer::AttributeType type, U32 first, S32 count)
    {
        if (!hasDataType(type) || first > mVertices || (count >= 0 && U32(count) > mVertices - first))
            return false;
        out = reinterpret_cast<T*>(getMappedData() + mOffsets[type] + std::size_t(first) * LLVertexBuffer::sTypeSize[type]);
        out.setStride(LLVertexBuffer::sTypeSize[type]);
        return true;
    }
};
