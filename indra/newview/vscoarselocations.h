// CPU coarse-location decoding shared by authenticated native transports. LGPL-2.1.
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <set>
#include <vector>
#include <utility>

template<class Identity> struct VSCoarseInput
{
    Identity id{};
    int x=0, y=0, z=0;
};
template<class Identity> struct VSCoarseSample
{
    Identity id{};
    std::array<double,3> position{};
    bool altitudeKnown=false;
};
template<class Identity> struct VSCoarseSnapshot
{
    std::vector<VSCoarseSample<Identity>> avatars;
    std::optional<std::array<double,3>> prey;
};
// Decode into a temporary; malformed input never partially replaces a circuit's
// last authenticated snapshot. HTTP excludes Prey; the legacy UDP path retains
// it in the avatar list, matching the respective reference handlers.
template<class Identity>
bool vs_decode_coarse(const std::vector<VSCoarseInput<Identity>>& rows, int you, int prey,
                     unsigned width, unsigned height, const std::array<double,3>& origin,
                     bool legacyUDP, VSCoarseSnapshot<Identity>& result)
{
    if (rows.size()>4096 || you < -1 || prey < -1 ||
        (you>=0 && std::size_t(you)>=rows.size()) || (prey>=0 && std::size_t(prey)>=rows.size()) ||
        width<16 || height<16 || width>65536 || height>65536 || width%16 || height%16 ||
        !std::isfinite(origin[0]) || !std::isfinite(origin[1]) || !std::isfinite(origin[2])) return false;
    VSCoarseSnapshot<Identity> next;
    next.avatars.reserve(rows.size());
    for(std::size_t i=0;i<rows.size();++i)
    {
        const auto& row=rows[i];
        if(row.x<0 || row.x>255 || row.y<0 || row.y>255 || row.z<0 || row.z>255) return false;
        const std::array<double,3> position={origin[0]+double(row.x)*width/256.0,
            origin[1]+double(row.y)*height/256.0,origin[2]+double(row.z)*4.0};
        if(int(i)==prey) next.prey=position;
        if(int(i)==you || (!legacyUDP && int(i)==prey)) continue;
        next.avatars.push_back({row.id,position,row.z!=0 && row.z!=255});
    }
    result=std::move(next);
    return true;
}

// Precise scene locations take precedence. Apply range filtering to the encoded
// coarse height before exposing the reference unknown-altitude sentinel.
template<class Identity>
std::vector<VSCoarseSample<Identity>> vs_merge_coarse(
    const std::vector<VSCoarseSample<Identity>>& precise,
    const std::vector<VSCoarseSample<Identity>>& coarse,
    const std::array<double,3>& relative, double radius)
{
    std::vector<VSCoarseSample<Identity>> result;
    if(!std::isfinite(radius) || radius<0 || !std::isfinite(relative[0]) ||
       !std::isfinite(relative[1]) || !std::isfinite(relative[2])) return result;
    std::set<Identity> seen;
    auto append=[&](const auto& rows, bool approximate)
    {
        for(auto row:rows)
        {
            const auto& p=row.position;
            if(row.id==Identity{} || !std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2])) continue;
            const double x=p[0]-relative[0],y=p[1]-relative[1],z=p[2]-relative[2];
            if(x*x+y*y+z*z>radius*radius || !seen.insert(row.id).second) continue;
            if(approximate && !row.altitudeKnown) row.position[2]=-1.0;
            result.push_back(row);
        }
    };
    append(precise,false);append(coarse,true);
    return result;
}
