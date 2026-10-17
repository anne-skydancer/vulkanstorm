// Immutable simulator parcel-overlay chunks. LGPL-2.1, like the viewer.
#pragma once
#include "vsresourcebudget.h"
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <cstring>

class VSParcelOverlay
{
    struct Chunk
    {
        std::array<std::uint8_t,1024> bytes;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
    };
    using Chunks=std::map<std::uint32_t,std::shared_ptr<const Chunk>>;
    std::uint32_t mWidth=0,mHeight=0;
    Chunks mChunks;
    std::shared_ptr<const VSResourceBudget::Reservation> mResidency;
public:
    using Snapshot=std::shared_ptr<const VSParcelOverlay>;
    using Reserve=std::function<std::shared_ptr<const VSResourceBudget::Reservation>(std::size_t)>;
    static Snapshot updated(const Snapshot& previous,std::uint32_t width,std::uint32_t height,
                            int sequence,const std::uint8_t* data,std::size_t bytes,const Reserve& reserve)
    {
        if(width<16 || height<16 || width>65536 || height>65536 || width%4 || height%4 ||
           sequence<0 || bytes!=1024 || !data || !reserve)return {};
        const auto cells=std::uint64_t(width/4)*(height/4);
        if(cells%1024 || std::uint64_t(sequence)>=cells/1024)return {};
        const bool reusable=previous && previous->mWidth==width && previous->mHeight==height;
        const auto* old=reusable?&previous->mChunks:nullptr;
        const auto count=old?old->size()+(old->count(std::uint32_t(sequence))?0:1):1;
        // Charge both immutable metadata copies and chunks while old snapshots
        // remain in use; refusal preserves the complete previous publication.
        auto metadata=reserve(sizeof(VSParcelOverlay)+count*(sizeof(Chunks::value_type)+4*sizeof(void*)));
        if(!metadata)return {};
        auto storage=reserve(sizeof(Chunk));if(!storage)return {};
        auto chunk=std::make_shared<Chunk>();std::memcpy(chunk->bytes.data(),data,1024);chunk->residency=std::move(storage);
        auto next=std::make_shared<VSParcelOverlay>();next->mWidth=width;next->mHeight=height;
        if(old)
        {
            next->mChunks=*old;
        }
        next->mChunks[std::uint32_t(sequence)]=std::move(chunk);
        next->mResidency=std::move(metadata);
        return next;
    }
    std::optional<std::uint8_t> cell(std::uint32_t column,std::uint32_t row)const
    {
        if(column>=mWidth/4 || row>=mHeight/4)return {};
        const auto index=std::uint64_t(row)*(mWidth/4)+column;
        const auto chunk=mChunks.find(std::uint32_t(index/1024));
        return chunk==mChunks.end()?std::optional<std::uint8_t>{}:chunk->second->bytes[index%1024];
    }
    std::uint32_t width()const{return mWidth;}
    std::uint32_t height()const{return mHeight;}
};
