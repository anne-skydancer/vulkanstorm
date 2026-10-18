// Completed native avatar-query coverage; unavailable is never a measured zero.
// LGPL-2.1.
#pragma once
#include <optional>
#include <algorithm>
#include <cmath>
namespace VSAvatarPerformance
{
struct Summary
{
    unsigned owners=0,sampled=0,averageCount=0;
    std::optional<double> total,average,maximum;
};
template<class Rows>Summary summarize(const Rows& rows)
{
    Summary result;double total=0,maximum=0,average=0;
    for(const auto& row:rows)
    {
        ++result.owners;
        if(!row.tuningMilliseconds || !std::isfinite(*row.tuningMilliseconds) || *row.tuningMilliseconds<0)continue;
        const double time=*row.tuningMilliseconds;++result.sampled;total+=time;maximum=std::max(maximum,time);
        if(!row.tooSlow){average+=time;++result.averageCount;}
    }
    if(result.sampled)
    {
        result.total=total;result.maximum=maximum;
        if(result.averageCount)result.average=average/result.averageCount;
    }
    return result;
}
}
