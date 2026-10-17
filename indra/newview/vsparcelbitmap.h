// Immutable parcel-property bitmaps and the shared boundary rule. LGPL-2.1.
#pragma once
#include "vsresourcebudget.h"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <limits>
#include <functional>
#include <memory>
#include <optional>
#include <vector>
class VSParcelBitmap
{
public:
    using Snapshot=std::shared_ptr<const VSParcelBitmap>;
    using Reserve=std::function<std::shared_ptr<const VSResourceBudget::Reservation>(std::size_t)>;
    static Snapshot fromWire(std::uint32_t columns,std::uint32_t rows,const std::uint8_t* data,
                             std::size_t bytes,const Reserve& reserve)
    {
        const auto cells=std::uint64_t(columns)*rows;
        if(!columns || !rows || !cells || cells%8 || cells/8!=bytes || bytes>std::numeric_limits<std::size_t>::max()-sizeof(VSParcelBitmap) || !data || !reserve)return {};
        auto residency=reserve(sizeof(VSParcelBitmap)+bytes);if(!residency)return {};
        auto next=std::make_shared<VSParcelBitmap>();next->mColumns=columns;next->mRows=rows;
        next->mBytes.assign(data,data+bytes);next->mResidency=std::move(residency);
        return next;
    }
    std::optional<bool> cell(std::uint32_t column,std::uint32_t row)const
    {
        if(column>=mColumns || row>=mRows)return {};
        const auto index=std::uint64_t(row)*mColumns+column;
        return (mBytes[index/8] & (1u<<(index%8)))!=0;
    }
    std::uint32_t columns()const{return mColumns;}
    std::uint32_t rows()const{return mRows;}
    const std::vector<std::uint8_t>& bytes()const{return mBytes;}
private:
    std::uint32_t mColumns=0,mRows=0;
    std::vector<std::uint8_t> mBytes;
    std::shared_ptr<const VSResourceBudget::Reservation> mResidency;
};
// Same XOR rule as LLViewerParcelMgr::writeSegmentsFromBitmap: shared cell
// edges disappear, leaving only the actual parcel boundary. Dimensions are explicit.
inline bool vs_write_parcel_segments(const std::uint8_t* bitmap,std::size_t bitmapBytes,
    std::uint32_t columns,std::uint32_t rows,std::uint8_t* segments,std::size_t segmentBytes,
    std::uint8_t southMask,std::uint8_t westMask)
{
    const auto cells=std::uint64_t(columns)*rows;
    const auto stride64=std::uint64_t(columns)+1;
    if(std::uint64_t(rows)+1>std::numeric_limits<std::size_t>::max()/stride64)return false;
    const auto segmentCount=stride64*(std::uint64_t(rows)+1);
    if(!columns || !rows || !bitmap || !segments || cells%8 || cells/8!=bitmapBytes || segmentCount!=segmentBytes)return false;
    const std::size_t stride=std::size_t(columns)+1;
    for(std::uint32_t y=0;y<rows;++y)
        for(std::uint32_t x=0;x<columns;++x)
        {
            const auto index=std::size_t(y)*columns+x;
            if(!(bitmap[index/8] & (1u<<(index%8))))continue;
            const auto out=std::size_t(y)*stride+x;
            segments[out]^=southMask;segments[out+stride]^=southMask;
            segments[out]^=westMask;segments[out+1]^=westMask;
        }
    return true;
}
