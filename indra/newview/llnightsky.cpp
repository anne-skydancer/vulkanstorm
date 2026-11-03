/**
 * @file llnightsky.cpp
 * @brief The real night sky: the naked-eye stars and the Milky Way, seen from a place on Earth
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * $/LicenseInfo$
 */

// Not the viewer's precompiled header: the integration test builds this
// file with llcommon and llmath only.
#include "linden_common.h"

#include "llnightsky.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#ifdef LL_USESYSTEMLIBS
# include <zlib.h>
#else
# include "zlib-ng/zlib.h"
#endif

namespace
{
    constexpr char MAGIC[8] = { 'V', 'K', 'S', 'S', 'T', 'A', 'R', '1' };
    constexpr char MILKY_WAY_MAGIC[8] = { 'V', 'K', 'S', 'M', 'W', 'A', 'Y', '1' };
    constexpr size_t MILKY_WAY_TEXELS = (size_t)LLNightSky::MILKY_WAY_WIDTH * LLNightSky::MILKY_WAY_HEIGHT;
    constexpr S16 NO_COLOUR = 32767;
    constexpr F64 PI = 3.14159265358979323846;
    constexpr F64 TAU = 2.0 * PI;
    // Days of the J2000.0 epoch (2000-01-01 12:00 TT) after the Unix epoch.
    constexpr F64 J2000_UNIX_DAYS = 10957.5;

    U16 read_u16(const U8* p) { return (U16)(p[0] | (p[1] << 8)); }
    S16 read_s16(const U8* p) { return (S16)read_u16(p); }
    U32 read_u32(const U8* p) { return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24); }

    // Inflates a zlib stream that must hold exactly expected bytes.
    bool inflate_exactly(const U8* packed, size_t length, size_t expected, std::vector<U8>& raw, std::string& error)
    {
        // one byte more than expected, to tell a longer stream
        raw.assign(expected + 1, 0);
        z_stream z;
        std::memset(&z, 0, sizeof(z));
        if (inflateInit(&z) != Z_OK)
        {
            error = "Milky Way map: inflateInit failed";
            return false;
        }
        z.next_in = const_cast<Bytef*>(packed);
        z.avail_in = (uInt)length;
        z.next_out = raw.data();
        z.avail_out = (uInt)raw.size();
        const int result = inflate(&z, Z_FINISH);
        const size_t unpacked = raw.size() - z.avail_out;
        inflateEnd(&z);
        if (result != Z_STREAM_END)
        {
            error = std::string("Milky Way map: ") + (z.msg ? z.msg : "damaged or truncated zlib stream");
            return false;
        }
        if (unpacked != expected)
        {
            error = "Milky Way map: " + std::to_string(unpacked) + " bytes unpacked";
            return false;
        }
        raw.resize(expected);
        return true;
    }

    F64 radians(F64 degrees) { return degrees * PI / 180.0; }

    struct Vec3d
    {
        F64 x, y, z;
    };

    Vec3d add(const Vec3d& a, const Vec3d& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    Vec3d scale(const Vec3d& a, F64 s) { return { a.x * s, a.y * s, a.z * s }; }

    // The Sun's right ascension (radians), n days after J2000.0: the
    // low-precision solar position of the Astronomical Almanac (about 0.01°).
    F64 sun_right_ascension(F64 n)
    {
        const F64 l = radians(280.460 + 0.9856474 * n);
        const F64 g = radians(357.528 + 0.9856003 * n);
        const F64 lambda = l + radians(1.915 * std::sin(g) + 0.020 * std::sin(2.0 * g));
        const F64 epsilon = radians(23.439 - 0.0000004 * n);
        return std::atan2(std::cos(epsilon) * std::sin(lambda), std::cos(lambda));
    }

    // Hour angle (radians, 0 at noon, growing westwards) of a direction of
    // the region's sky seen from latitude lat.
    F64 sun_hour_angle(const Vec3d& s, F64 lat)
    {
        return std::atan2(-s.x, s.z * std::cos(lat) - s.y * std::sin(lat));
    }

    // Cells a star's point can reach: its own and those within MARGIN.
    void cells_around(const LLVector3& d, std::vector<U32>& out)
    {
        out.clear();
        out.push_back(LLNightSky::cellOf(d));
        const LLVector3 side = std::fabs(d.mV[VZ]) < 0.9f ? LLVector3(0.f, 0.f, 1.f) : LLVector3(1.f, 0.f, 0.f);
        LLVector3 e1 = d % side;
        e1.normVec();
        const LLVector3 e2 = d % e1;
        // rings out to a little past the margin, closer than a cell's corner
        // can slip between two samples
        for (F32 ring : { 0.25f, 0.5f, 0.75f, 1.0f, 1.08f })
        {
            for (S32 k = 0; k < 32; ++k)
            {
                const F32 a = (F32)k / 32.f * (F32)TAU;
                const LLVector3 p = d + (e1 * std::cos(a) + e2 * std::sin(a)) * (LLNightSky::MARGIN * ring);
                const U32 c = LLNightSky::cellOf(p);
                if (std::find(out.begin(), out.end(), c) == out.end())
                {
                    out.push_back(c);
                }
            }
        }
    }
}

bool LLNightSky::GpuStar::operator==(const GpuStar& other) const
{
    return std::memcmp(pos, other.pos, sizeof(pos)) == 0 && std::memcmp(color, other.color, sizeof(color)) == 0;
}

bool LLNightSky::parse(const U8* data, size_t size, std::vector<CatalogueStar>& stars, std::string& error)
{
    stars.clear();
    if (!data || size < 12)
    {
        error = "star table: too short";
        return false;
    }
    if (std::memcmp(data, MAGIC, sizeof(MAGIC)) != 0)
    {
        error = "star table: bad magic";
        return false;
    }
    const U32 count = (U32)data[8] | ((U32)data[9] << 8) | ((U32)data[10] << 16) | ((U32)data[11] << 24);
    if ((size - 12) / 8 != count || (size - 12) % 8 != 0)
    {
        error = "star table: " + std::to_string(size - 12) + " bytes for " + std::to_string(count) + " stars";
        return false;
    }
    stars.reserve(count);
    for (const U8* c = data + 12; c < data + size; c += 8)
    {
        const S16 bv = read_s16(c + 6);
        CatalogueStar s;
        s.ra = (F32)read_u16(c) / 65536.f * (F32)TAU;
        s.dec = (F32)radians((F32)read_s16(c + 2) / 32767.f * 90.f);
        s.magnitude = (F32)read_s16(c + 4) / 100.f;
        s.has_bv = bv != NO_COLOUR;
        s.bv = s.has_bv ? (F32)bv / 1000.f : 0.f;
        stars.push_back(s);
    }
    return true;
}

bool LLNightSky::parseMilkyWay(const U8* data, size_t size, std::vector<U32>& texels, std::string& error)
{
    texels.clear();
    if (!data || size < 32)
    {
        error = "Milky Way map: too short";
        return false;
    }
    if (std::memcmp(data, MILKY_WAY_MAGIC, sizeof(MILKY_WAY_MAGIC)) != 0)
    {
        error = "Milky Way map: bad magic";
        return false;
    }
    const U32 width = read_u32(data + 8);
    const U32 height = read_u32(data + 12);
    const U32 reach_bits = read_u32(data + 16);
    F32 reach;
    std::memcpy(&reach, &reach_bits, sizeof(reach));
    const size_t cw = read_u32(data + 20);
    const size_t ch = read_u32(data + 24);
    const size_t length = read_u32(data + 28);
    if (width != MILKY_WAY_WIDTH || height != MILKY_WAY_HEIGHT || reach != MILKY_WAY_REACH)
    {
        error = "Milky Way map: " + std::to_string(width) + " x " + std::to_string(height) + " within "
            + std::to_string(reach) + " degrees, not the shader's";
        return false;
    }
    // the colour grid is coarser than the map: this bounds the unpacked size
    // before anything is allocated for a damaged header
    if (cw == 0 || ch == 0 || cw > MILKY_WAY_WIDTH || ch > MILKY_WAY_HEIGHT)
    {
        error = "Milky Way map: colour grid " + std::to_string(cw) + " x " + std::to_string(ch);
        return false;
    }
    if (length > size - 32)
    {
        error = "Milky Way map: truncated";
        return false;
    }
    std::vector<U8> raw;
    if (!inflate_exactly(data + 32, length, MILKY_WAY_TEXELS * 2 + cw * ch, raw, error))
    {
        return false;
    }
    const U8* colour = raw.data() + MILKY_WAY_TEXELS * 2;
    const size_t w = MILKY_WAY_WIDTH;
    const size_t h = MILKY_WAY_HEIGHT;
    // the colour grid's value at a texel: bilinear between the centres of
    // its cells (longitude wraps)
    auto colour_at = [&](size_t x, size_t y) -> U32
    {
        const F32 fx = ((F32)x + 0.5f) * (F32)cw / (F32)w - 0.5f;
        const F32 fy = std::clamp(((F32)y + 0.5f) * (F32)ch / (F32)h - 0.5f, 0.f, (F32)(ch - 1));
        const F32 x0 = std::floor(fx);
        const F32 y0 = std::floor(fy);
        const F32 tx = fx - x0;
        const F32 ty = fy - y0;
        const S64 n = (S64)cw;
        const size_t xa = (size_t)((((S64)x0 % n) + n) % n);
        const size_t xb = (xa + 1) % cw;
        const size_t ya = (size_t)y0;
        const size_t yb = std::min(ya + 1, ch - 1);
        auto c = [&](size_t cx, size_t cy) { return (F32)colour[cy * cw + cx]; };
        const F32 top = c(xa, ya) + (c(xb, ya) - c(xa, ya)) * tx;
        const F32 bottom = c(xa, yb) + (c(xb, yb) - c(xa, yb)) * tx;
        return (U32)std::round(top + (bottom - top) * ty);
    };
    texels.resize(MILKY_WAY_TEXELS);
    for (size_t i = 0; i < MILKY_WAY_TEXELS; ++i)
    {
        texels[i] = (U32)read_u16(raw.data() + i * 2) | (colour_at(i % w, i / w) << 16);
    }
    return true;
}

LLVector3 LLNightSky::equatorial(F32 ra, F32 dec)
{
    return LLVector3(std::cos(dec) * std::cos(ra), std::cos(dec) * std::sin(ra), std::sin(dec));
}

F32 LLNightSky::intensity(F32 magnitude)
{
    return std::min(2.f * std::pow(10.f, -0.4f * 0.75f * magnitude), 3.f);
}

F32 LLNightSky::pointSize(F32 magnitude)
{
    return 0.6f + 0.6f * std::clamp((3.f - magnitude) / 4.f, 0.f, 1.f);
}

// Temperature from Ballesteros (2012), black body colour from Tanner
// Helland's fit, then half desaturated: the eye sees little colour in
// starlight.
LLVector3 LLNightSky::colour(bool has_bv, F32 bv)
{
    if (!has_bv)
    {
        return LLVector3(1.f, 1.f, 1.f);
    }
    bv = std::clamp(bv, -0.4f, 2.f);
    F32 t = 4600.f * (1.f / (0.92f * bv + 1.7f) + 1.f / (0.92f * bv + 0.62f));
    t /= 100.f;
    const F32 r = t <= 66.f ? 255.f : 329.7f * std::pow(t - 60.f, -0.1332f);
    const F32 g = t <= 66.f ? 99.47f * std::log(t) - 161.12f : 288.12f * std::pow(t - 60.f, -0.0755f);
    const F32 b = t >= 66.f ? 255.f : (t <= 19.f ? 0.f : 138.52f * std::log(t - 10.f) - 305.04f);
    F32 c[3] = { r, g, b };
    F32 luma = 0.f;
    const F32 weights[3] = { 0.2126f, 0.7152f, 0.0722f };
    for (S32 i = 0; i < 3; ++i)
    {
        c[i] = std::pow(std::clamp(c[i], 0.f, 255.f) / 255.f, 2.2f);
        luma += c[i] * weights[i];
    }
    luma = std::max(luma, 1e-4f);
    LLVector3 out;
    for (S32 i = 0; i < 3; ++i)
    {
        out.mV[i] = std::min(c[i] / luma * 0.5f + 0.5f, 2.f);
    }
    return out;
}

std::vector<LLNightSky::GpuStar> LLNightSky::gpuStars(const std::vector<CatalogueStar>& stars)
{
    std::vector<GpuStar> out;
    out.reserve(stars.size());
    for (const CatalogueStar& s : stars)
    {
        const LLVector3 d = equatorial(s.ra, s.dec);
        const LLVector3 c = colour(s.has_bv, s.bv);
        out.push_back({ { d.mV[VX], d.mV[VY], d.mV[VZ], intensity(s.magnitude) },
                        { c.mV[VX], c.mV[VY], c.mV[VZ], pointSize(s.magnitude) } });
    }
    return out;
}

U32 LLNightSky::cellOf(const LLVector3& q)
{
    const F32 ax = std::fabs(q.mV[VX]);
    const F32 ay = std::fabs(q.mV[VY]);
    const F32 az = std::fabs(q.mV[VZ]);
    U32 face;
    F32 u, v, m;
    if (ax >= ay && ax >= az)
    {
        face = q.mV[VX] >= 0.f ? 0 : 1;
        u = q.mV[VY];
        v = q.mV[VZ];
        m = ax;
    }
    else if (ay >= az)
    {
        face = q.mV[VY] >= 0.f ? 2 : 3;
        u = q.mV[VX];
        v = q.mV[VZ];
        m = ay;
    }
    else
    {
        face = q.mV[VZ] >= 0.f ? 4 : 5;
        u = q.mV[VX];
        v = q.mV[VY];
        m = az;
    }
    const F32 n = (F32)FACE_CELLS;
    m = std::max(m, 1e-20f);
    const U32 cx = (U32)std::clamp(std::floor((u / m * 0.5f + 0.5f) * n), 0.f, n - 1.f);
    const U32 cy = (U32)std::clamp(std::floor((v / m * 0.5f + 0.5f) * n), 0.f, n - 1.f);
    return face * FACE_CELLS * FACE_CELLS + cy * FACE_CELLS + cx;
}

void LLNightSky::bin(const std::vector<GpuStar>& stars, std::vector<U32>& offsets, std::vector<GpuStar>& binned)
{
    std::vector<std::vector<U32>> lists(CELLS);
    std::vector<U32> around;
    for (U32 i = 0; i < (U32)stars.size(); ++i)
    {
        cells_around(LLVector3(stars[i].pos[0], stars[i].pos[1], stars[i].pos[2]), around);
        for (U32 c : around)
        {
            lists[c].push_back(i);
        }
    }
    offsets.clear();
    offsets.reserve(CELLS + 1);
    binned.clear();
    for (const std::vector<U32>& list : lists)
    {
        offsets.push_back((U32)binned.size());
        for (U32 i : list)
        {
            binned.push_back(stars[i]);
        }
    }
    offsets.push_back((U32)binned.size());
}

F64 LLNightSky::unixDaysNow()
{
    using namespace std::chrono;
    return duration<F64>(system_clock::now().time_since_epoch()).count() / 86400.0;
}

F64 LLNightSky::greenwichSidereal(F64 n)
{
    const F64 gmst = std::fmod(280.46061837 + 360.98564736629 * n, 360.0);
    return gmst < 0.0 ? gmst + 360.0 : gmst;
}

LLNightSky::SkyPlace LLNightSky::skyPlace(U32 setting)
{
    return setting < SKY_PLACES ? (SkyPlace)setting : SkyPlace::Rome;
}

bool LLNightSky::placeLatitude(SkyPlace place, F64& out)
{
    switch (place)
    {
    case SkyPlace::Rome:         out = LATITUDE; return true;
    case SkyPlace::Paris:        out = 48.8566; return true;
    case SkyPlace::London:       out = 51.507; return true;
    case SkyPlace::NewYork:      out = 40.713; return true;
    case SkyPlace::LosAngeles:   out = 34.052; return true;
    case SkyPlace::Tokyo:        out = 35.676; return true;
    case SkyPlace::RioDeJaneiro: out = -22.907; return true;
    case SkyPlace::CapeTown:     out = -33.925; return true;
    case SkyPlace::Sydney:       out = -33.869; return true;
    case SkyPlace::Equator:      out = 0.0; return true;
    case SkyPlace::NorthPole:    out = 90.0; return true;
    case SkyPlace::Custom:       break;
    }
    return false;
}

bool LLNightSky::placeLongitude(SkyPlace place, F64& out)
{
    switch (place)
    {
    case SkyPlace::Rome:         out = LONGITUDE; return true;
    case SkyPlace::Paris:        out = 2.3522; return true;
    case SkyPlace::London:       out = -0.128; return true;
    case SkyPlace::NewYork:      out = -74.006; return true;
    case SkyPlace::LosAngeles:   out = -118.244; return true;
    case SkyPlace::Tokyo:        out = 139.650; return true;
    case SkyPlace::RioDeJaneiro: out = -43.173; return true;
    case SkyPlace::CapeTown:     out = 18.424; return true;
    case SkyPlace::Sydney:       out = 151.209; return true;
    case SkyPlace::Equator:      out = 0.0; return true;
    case SkyPlace::NorthPole:    out = 0.0; return true;
    case SkyPlace::Custom:       break;
    }
    return false;
}

LLNightSky::Observer LLNightSky::observer(SkyPlace place, F64 latitude, F64 longitude, StarHour hour)
{
    Observer at;
    at.hour = hour;
    if (!placeLatitude(place, at.latitude))
    {
        at.latitude = std::isfinite(latitude) ? std::clamp(latitude, -90.0, 90.0) : LATITUDE;
    }
    if (!placeLongitude(place, at.longitude))
    {
        at.longitude = std::isfinite(longitude) ? std::clamp(longitude, -180.0, 180.0) : LONGITUDE;
    }
    return at;
}

void LLNightSky::axes(const LLVector3& sun, F64 unix_days, LLVector3 out[3], StarHour hour, F64 longitude)
{
    Observer at;
    at.hour = hour;
    at.longitude = longitude;
    axes(sun, unix_days, out, at);
}

void LLNightSky::axes(const LLVector3& sun, F64 unix_days, LLVector3 out[3], const Observer& at)
{
    const F64 lat = radians(std::isfinite(at.latitude) ? std::clamp(at.latitude, -90.0, 90.0) : LATITUDE);
    F64 lst;
    if (at.hour == StarHour::RealTime)
    {
        // local sidereal time: Greenwich's from the clock plus the longitude
        const F64 east = std::isfinite(at.longitude) ? at.longitude : LONGITUDE;
        lst = radians(greenwichSidereal(unix_days - J2000_UNIX_DAYS) + east);
    }
    else
    {
        // local sidereal time: the Sun's right ascension plus its hour
        // angle in the region's sky
        const Vec3d s = { sun.mV[VX], sun.mV[VY], sun.mV[VZ] };
        lst = sun_right_ascension(unix_days - J2000_UNIX_DAYS) + sun_hour_angle(s, lat);
    }
    // to hour angle coordinates (a turn by -lst about the pole), then
    // tilted by the latitude: the tilt's columns are the region directions
    // of the hour angle frame's axes
    const Vec3d tilt[3] = {
        { 0.0, -std::sin(lat), std::cos(lat) },
        { 1.0, 0.0, 0.0 },
        { 0.0, std::cos(lat), std::sin(lat) },
    };
    const F64 c = std::cos(lst);
    const F64 sn = std::sin(lst);
    const Vec3d turn[3] = {
        { c, -sn, 0.0 },
        { sn, c, 0.0 },
        { 0.0, 0.0, 1.0 },
    };
    for (S32 i = 0; i < 3; ++i)
    {
        const Vec3d col = add(add(scale(tilt[0], turn[i].x), scale(tilt[1], turn[i].y)), scale(tilt[2], turn[i].z));
        out[i].setVec((F32)col.x, (F32)col.y, (F32)col.z);
    }
}
