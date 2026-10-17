// Owner-bound echoed request tags for native parcel UI channels. LGPL-2.1.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
class VSParcelRequests
{
public:
    enum class Channel{Selection,Hover,Access};
    struct Pending{std::int32_t wire=0,logical=0,local=-1;std::function<bool()> current;};
    std::optional<std::int32_t> issue(Channel channel,std::int32_t logical,std::function<bool()> current,std::int32_t local=-1,
        std::uint32_t flags=std::numeric_limits<std::uint32_t>::max())
    {
        if(std::size_t(channel)>=mPending.size() || !current || !current())return {};
        // Never reuse an echoed tag across selection, region, account or ledger retirement.
        if(sNext==std::numeric_limits<std::int32_t>::min())return {};
        const auto wire=--sNext;
        if(channel==Channel::Access)
        {
            for(unsigned bit=0;bit<32;++bit)
                if(flags&(std::uint32_t(1)<<bit))mAccess[bit]=Pending{wire,logical,local,current};
        }
        else mPending[std::size_t(channel)]=Pending{wire,logical,local,std::move(current)};
        return wire;
    }
    std::optional<Pending> match(Channel channel,std::int32_t wire,std::optional<std::int32_t> local={},
        std::uint32_t flags=std::numeric_limits<std::uint32_t>::max())const
    {
        if(std::size_t(channel)>=mPending.size())return {};
        if(channel==Channel::Access)
        {
            // Access, ban, allowed/blocked experiences are independent reads.
            // A newer ban request must not discard the current access chunks.
            for(unsigned bit=0;bit<32;++bit)
            {
                if(!(flags&(std::uint32_t(1)<<bit)))continue;
                const auto pending=mAccess[bit];
                if(!pending || pending->wire!=wire || (local && pending->local!=*local) || !pending->current || !pending->current())continue;
                const auto& active=mAccess[bit];
                if(active && active->wire==pending->wire)return pending;
            }
            return {};
        }
        const auto pending=mPending[std::size_t(channel)];
        if(!pending || pending->wire!=wire || (local && pending->local!=*local) || !pending->current || !pending->current())return {};
        const auto& active=mPending[std::size_t(channel)];
        return active && active->wire==pending->wire ? pending : std::nullopt;
    }
    std::optional<Pending> properties(std::int32_t wire)const
    {
        for(auto channel:{Channel::Selection,Channel::Hover})if(auto found=match(channel,wire))return found;
        return {};
    }
    void retire(Channel channel)
    {
        if(std::size_t(channel)<mPending.size())mPending[std::size_t(channel)].reset();
        if(channel==Channel::Access)for(auto& pending:mAccess)pending.reset();
    }
    void retire(){for(auto& pending:mPending)pending.reset();for(auto& pending:mAccess)pending.reset();}
private:
    inline static std::int32_t sNext=-1000000;
    std::array<std::optional<Pending>,3> mPending;
    std::array<std::optional<Pending>,32> mAccess;
};
