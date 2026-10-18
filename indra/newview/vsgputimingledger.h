// Nonblocking ordered GPU timing aggregation, shared with source regressions.
// LGPL-2.1.
#pragma once
#include <map>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>
struct VSGPUTimingResult
{
    bool ready=false,valid=false;
    double milliseconds=0;
};
template<class Owner,class Query>class VSGPUTimingLedger
{
    struct Sample {Query query;VSGPUTimingResult result;unsigned long long lastPoll=0;};
    struct Span {std::shared_ptr<Sample> sample;bool ready=false;};
    struct Row
    {
        std::vector<Span> spans;
        double milliseconds=0;
        bool valid=true,published=false;
    };
    std::map<std::shared_ptr<const Owner>,Row,std::owner_less<std::shared_ptr<const Owner>>> mRows;
    unsigned long long mPoll=0;
public:
    std::shared_ptr<const void> residency;
    void add(const std::shared_ptr<const Owner>& owner,Query query)
    {addShared({owner},std::move(query));}
    // One real draw query may contribute to both its avatar and attachment.
    // Cache/recycle the physical query once; each owner aggregates independently.
    void addShared(std::initializer_list<std::shared_ptr<const Owner>> owners,Query query)
    {
        auto sample=std::make_shared<Sample>(Sample{std::move(query),{}});
        for(const auto& owner:owners)
        {
            if(!owner)continue;
            auto& row=mRows[owner];
            if(!row.spans.empty() && row.spans.back().sample==sample)continue;
            row.spans.push_back({sample,false});
        }
    }
    void refuse(const std::shared_ptr<const Owner>& owner)
    {mRows[owner].valid=false;}
    template<class Read,class Recycle>bool poll(Read read,Recycle recycle)
    {
        const auto poll=++mPoll;
        bool finished=true;
        for(auto& entry:mRows)
        {
            auto& row=entry.second;if(row.published)continue;
            bool ready=true;
            for(auto& span:row.spans)
            {
                if(span.ready)continue;
                auto& sample=*span.sample;
                if(!sample.result.ready && sample.lastPoll!=poll)
                {
                    sample.lastPoll=poll;
                    sample.result=read(sample.query);
                    if(sample.result.ready)recycle(std::move(sample.query));
                }
                if(!sample.result.ready){ready=false;continue;}
                span.ready=true;row.valid=row.valid && sample.result.valid;
                if(sample.result.valid)row.milliseconds+=sample.result.milliseconds;
            }
            if(!ready){finished=false;continue;}
            row.published=true; // Reentrant account retirement cannot publish twice.
            if(row.valid && !row.spans.empty() && entry.first && entry.first->current())
                entry.first->publish(float(row.milliseconds));
        }
        return finished;
    }
};
