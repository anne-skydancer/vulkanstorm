// See vsobjecteditparams.h. LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsobjecteditparams.h"
#include "lldatapacker.h"

LLNetworkData* VSObjectEditParameters::get(U16 type)
{
    switch (type)
    {
        case LLNetworkData::PARAMS_FLEXIBLE:
            return &flexible;
        case LLNetworkData::PARAMS_LIGHT:
            return &light;
        case LLNetworkData::PARAMS_LIGHT_IMAGE:
            return &lightImage;
        case LLNetworkData::PARAMS_SCULPT:
        case LLNetworkData::PARAMS_MESH:
            return &sculpt;
        case LLNetworkData::PARAMS_EXTENDED_MESH:
            return &mesh;
        case LLNetworkData::PARAMS_REFLECTION_PROBE:
            return &reflection;
        default:
            return nullptr;
    }
}
const LLNetworkData* VSObjectEditParameters::get(U16 type) const
{ return const_cast<VSObjectEditParameters*>(this)->get(type); }
bool VSObjectEditParameters::inUse(U16 type) const
{
    if (type == LLNetworkData::PARAMS_MESH)
        type = LLNetworkData::PARAMS_SCULPT;
    return get(type) && (mUsed & (1u << (type / 16))) != 0;
}
bool VSObjectEditParameters::setInUse(U16 type, bool enabled)
{
    if (type == LLNetworkData::PARAMS_MESH)
        type = LLNetworkData::PARAMS_SCULPT;
    if (!get(type))
        return false;
    const U16  bit     = U16(1u << (type / 16));
    const bool changed = inUse(type) != enabled;
    mUsed              = enabled ? U16(mUsed | bit) : U16(mUsed & ~bit);
    return changed;
}
bool VSObjectEditParameters::unpack(const std::vector<U8>& bytes)
{
    VSObjectEditParameters next;
    if (bytes.empty())
    {
        *this = std::move(next);
        return true;
    }
    std::size_t cursor = 1;
    for (U32 i = 0; i < bytes[0]; ++i)
    {
        if (bytes.size() - cursor < 6)
            return false;
        const U16 type = U16(bytes[cursor]) | U16(bytes[cursor + 1]) << 8;
        const U32 length =
            U32(bytes[cursor + 2]) | U32(bytes[cursor + 3]) << 8 | U32(bytes[cursor + 4]) << 16 | U32(bytes[cursor + 5]) << 24;
        cursor += 6;
        if (length > bytes.size() - cursor)
            return false;
        if (!next.unpack(type, bytes.data() + cursor, length))
            return false;
        cursor += length;
    }
    if (cursor != bytes.size())
        return false;
    *this = std::move(next);
    return true;
}
bool VSObjectEditParameters::unpack(U16 type, const U8* data, U32 size)
{
    auto* parameter = get(type);
    if (!parameter)
        return true; // Unrelated parameter blocks retain their existing scene consumers.
    const U16 canonicalType = type == LLNetworkData::PARAMS_MESH ? LLNetworkData::PARAMS_SCULPT : type;
    // The original flexible unpacker also handles the force-free four-byte
    // block accepted by this scene decoder; let that CPU parser read it directly.
    if (!LLNetworkData::isValid(canonicalType, size) && !(canonicalType == LLNetworkData::PARAMS_FLEXIBLE && size == 4))
        return false;
    LLDataPackerBinaryBuffer packed(const_cast<U8*>(data), S32(size));
    if (!parameter->unpack(packed))
        return false;
    setInUse(type, true);
    return true;
}
