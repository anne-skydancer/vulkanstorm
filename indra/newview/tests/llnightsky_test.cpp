/**
 * @file llnightsky_test.cpp
 * @brief The real night sky's catalogue, binning, frame and place
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

#include "linden_common.h"
#include "../llnightsky.h"
#include "../test/lltut.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace
{
    constexpr F32 DEG = 3.14159265f / 180.f;

    // a file of the source tree, found from this source file
    std::vector<U8> read_source(const std::string& relative)
    {
        std::string here = __FILE__;
        const size_t slash = here.find_last_of("/\\");
        const std::string path = here.substr(0, slash + 1) + relative;
        std::ifstream file(path, std::ios::binary);
        tut::ensure("open " + path, file.is_open());
        return std::vector<U8>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }

    // app_settings/stars/stars.bin
    std::vector<U8> read_catalogue()
    {
        return read_source("../app_settings/stars/stars.bin");
    }

    // app_settings/stars/milky_way.bin, decoded
    std::vector<U32> milky_way()
    {
        const std::vector<U8> bytes = read_source("../app_settings/stars/milky_way.bin");
        std::vector<U32> map;
        std::string error;
        const bool parsed = LLNightSky::parseMilkyWay(bytes.data(), bytes.size(), map, error);
        tut::ensure("milky_way.bin parses: " + error, parsed);
        return map;
    }

    bool map_parses(const std::vector<U8>& bytes)
    {
        std::vector<U32> map;
        std::string error;
        return LLNightSky::parseMilkyWay(bytes.data(), bytes.size(), map, error);
    }

    // The map's texel at a galactic longitude and latitude (degrees).
    S32 map_x(F32 l) { return (S32)((l + 180.f) / 360.f * (F32)LLNightSky::MILKY_WAY_WIDTH); }
    S32 map_y(F32 b)
    {
        return (S32)((b + LLNightSky::MILKY_WAY_REACH) / (2.f * LLNightSky::MILKY_WAY_REACH) * (F32)LLNightSky::MILKY_WAY_HEIGHT);
    }

    // The map's glow (0..1, linear) at a galactic longitude and latitude
    // (degrees), averaged over a few texels.
    F32 glow(const std::vector<U32>& map, F32 l, F32 b)
    {
        const S32 w = (S32)LLNightSky::MILKY_WAY_WIDTH;
        const S32 h = (S32)LLNightSky::MILKY_WAY_HEIGHT;
        const S32 x = map_x(l);
        const S32 y = map_y(b);
        F32 sum = 0.f;
        for (S32 dy = -2; dy <= 2; ++dy)
        {
            for (S32 dx = -2; dx <= 2; ++dx)
            {
                const U32 t = map[std::clamp(y + dy, 0, h - 1) * w + ((x + dx) % w + w) % w];
                const F32 g = (F32)(t & 255) / 255.f;
                sum += g * g;
            }
        }
        return sum / 25.f;
    }

    std::vector<LLNightSky::CatalogueStar> catalogue()
    {
        const std::vector<U8> bytes = read_catalogue();
        std::vector<LLNightSky::CatalogueStar> stars;
        std::string error;
        tut::ensure("stars.bin parses: " + error, LLNightSky::parse(bytes.data(), bytes.size(), stars, error));
        return stars;
    }

    std::vector<U8> table(const char* magic, U32 count, size_t body)
    {
        std::vector<U8> t(magic, magic + 8);
        for (S32 i = 0; i < 4; ++i)
        {
            t.push_back((U8)(count >> (8 * i)));
        }
        t.resize(t.size() + body, 0);
        return t;
    }

    bool parses(const std::vector<U8>& t)
    {
        std::vector<LLNightSky::CatalogueStar> stars;
        std::string error;
        return LLNightSky::parse(t.data(), t.size(), stars, error);
    }

    // A star (right ascension in hours, declination in degrees) in the
    // region's frame for this sun and date.
    LLVector3 region(const LLVector3& sun, F64 unix_days, F32 ra_hours, F32 dec_deg,
                     LLNightSky::StarHour hour = LLNightSky::StarHour::RegionSun, F64 longitude = LLNightSky::LONGITUDE)
    {
        LLVector3 axes[3];
        LLNightSky::axes(sun, unix_days, axes, hour, longitude);
        const LLVector3 e = LLNightSky::equatorial(ra_hours * 15.f * DEG, dec_deg * DEG);
        return axes[0] * e.mV[VX] + axes[1] * e.mV[VY] + axes[2] * e.mV[VZ];
    }

    // A star in the region's frame for this sun and date, seen by at.
    LLVector3 region_at(const LLNightSky::Observer& at, const LLVector3& sun, F64 unix_days, F32 ra_hours, F32 dec_deg)
    {
        LLVector3 axes[3];
        LLNightSky::axes(sun, unix_days, axes, at);
        const LLVector3 e = LLNightSky::equatorial(ra_hours * 15.f * DEG, dec_deg * DEG);
        return axes[0] * e.mV[VX] + axes[1] * e.mV[VY] + axes[2] * e.mV[VZ];
    }

    LLNightSky::Observer observer_at(F64 latitude, F64 longitude = LLNightSky::LONGITUDE,
                                     LLNightSky::StarHour hour = LLNightSky::StarHour::RegionSun)
    {
        LLNightSky::Observer at;
        at.latitude = latitude;
        at.longitude = longitude;
        at.hour = hour;
        return at;
    }

    // every place, Custom last
    std::vector<LLNightSky::SkyPlace> all_places()
    {
        std::vector<LLNightSky::SkyPlace> places;
        for (U32 i = 0; i < LLNightSky::SKY_PLACES; ++i)
        {
            places.push_back(LLNightSky::skyPlace(i));
        }
        return places;
    }

    // the sun straight below the horizon: the region's midnight
    const LLVector3 MIDNIGHT(0.f, 0.f, -1.f);
    // 2026-01-01 and 2026-07-01, 00:00 UTC
    constexpr F64 WINTER = 20454.0;
    constexpr F64 SUMMER = 20635.0;
}

namespace tut
{
    struct nightsky_data
    {
    };
    typedef test_group<nightsky_data> nightsky_group;
    typedef nightsky_group::object nightsky_object;
    tut::nightsky_group nightsky_test("LLNightSky");

    template<> template<>
    void nightsky_object::test<1>()
    {
        set_test_name("the table holds the naked-eye sky, brightest first");
        const std::vector<LLNightSky::CatalogueStar> stars = catalogue();
        ensure("8,000 to 10,000 stars", stars.size() >= 8000 && stars.size() < 10000);
        // Sirius: RA 6h45m, Dec -16.7°, magnitude -1.46
        const LLNightSky::CatalogueStar& sirius = stars[0];
        ensure("Sirius' right ascension", std::fabs(sirius.ra / DEG - 101.29f) < 0.1f);
        ensure("Sirius' declination", std::fabs(sirius.dec / DEG + 16.72f) < 0.1f);
        ensure("Sirius' magnitude", sirius.magnitude < -1.4f);
        for (size_t i = 1; i < stars.size(); ++i)
        {
            ensure("sorted by magnitude", stars[i - 1].magnitude <= stars[i].magnitude);
        }
        ensure("magnitude 6.5 at most", stars.back().magnitude <= 6.5f);
        // Polaris sits within a degree of the pole
        ensure("Polaris", std::any_of(stars.begin(), stars.end(), [](const LLNightSky::CatalogueStar& s)
            { return s.magnitude < 2.1f && s.dec / DEG > 89.f; }));
    }

    template<> template<>
    void nightsky_object::test<2>()
    {
        set_test_name("a damaged table is refused");
        ensure("header only", !parses(std::vector<U8>({ 'V', 'K', 'S', 'S', 'T', 'A', 'R', '1' })));
        ensure("bad magic", !parses(table("NOTSTARS", 1, 8)));
        ensure("Aurora's magic", !parses(table("AURSTAR1", 1, 8)));
        ensure("too few bytes", !parses(table("VKSSTAR1", 2, 8)));
        ensure("a partial star", !parses(table("VKSSTAR1", 1, 9)));
        ensure("an empty table", parses(table("VKSSTAR1", 0, 0)));
    }

    template<> template<>
    void nightsky_object::test<3>()
    {
        set_test_name("colours go from blue to orange");
        const LLVector3 rigel = LLNightSky::colour(true, -0.03f);
        const LLVector3 sun = LLNightSky::colour(true, 0.65f);
        const LLVector3 betelgeuse = LLNightSky::colour(true, 1.85f);
        ensure("Rigel is blue", rigel.mV[VZ] > rigel.mV[VX]);
        ensure("Betelgeuse is orange", betelgeuse.mV[VX] > betelgeuse.mV[VZ]);
        ensure("the Sun is between", sun.mV[VX] > sun.mV[VZ]
            && sun.mV[VX] - sun.mV[VZ] < betelgeuse.mV[VX] - betelgeuse.mV[VZ]);
        ensure("no index: white", LLNightSky::colour(false, 0.f) == LLVector3(1.f, 1.f, 1.f));
    }

    template<> template<>
    void nightsky_object::test<4>()
    {
        set_test_name("brighter stars are brighter and larger");
        ensure(LLNightSky::intensity(-1.46f) > LLNightSky::intensity(1.f));
        ensure(LLNightSky::intensity(1.f) > LLNightSky::intensity(6.5f));
        ensure(LLNightSky::intensity(6.5f) > 0.015f);
        ensure(LLNightSky::pointSize(-1.f) > LLNightSky::pointSize(5.f));
    }

    template<> template<>
    void nightsky_object::test<5>()
    {
        set_test_name("every star is found from any pixel of its point");
        const std::vector<LLNightSky::GpuStar> stars = LLNightSky::gpuStars(catalogue());
        std::vector<U32> offsets;
        std::vector<LLNightSky::GpuStar> binned;
        LLNightSky::bin(stars, offsets, binned);
        for (size_t s = 0; s < stars.size(); s += 7)
        {
            const LLVector3 d(stars[s].pos[0], stars[s].pos[1], stars[s].pos[2]);
            const LLVector3 side = std::fabs(d.mV[VZ]) < 0.9f ? LLVector3(0.f, 0.f, 1.f) : LLVector3(1.f, 0.f, 0.f);
            LLVector3 e1 = d % side;
            e1.normVec();
            const LLVector3 e2 = d % e1;
            for (S32 k = 0; k < 24; ++k)
            {
                const F32 a = (F32)k / 24.f * 2.f * 3.14159265f;
                for (F32 r : { 0.f, 0.3f, 0.6f, 0.95f })
                {
                    LLVector3 q = d + (e1 * std::cos(a) + e2 * std::sin(a)) * (LLNightSky::MARGIN * r);
                    q.normVec();
                    const U32 c = LLNightSky::cellOf(q);
                    ensure("star " + std::to_string(s) + " missed",
                        std::find(binned.begin() + offsets[c], binned.begin() + offsets[c + 1], stars[s])
                            != binned.begin() + offsets[c + 1]);
                }
            }
        }
    }

    template<> template<>
    void nightsky_object::test<6>()
    {
        set_test_name("the cell table covers the stars in order");
        std::vector<U32> offsets;
        std::vector<LLNightSky::GpuStar> binned;
        LLNightSky::bin(LLNightSky::gpuStars(catalogue()), offsets, binned);
        ensure_equals("one offset per cell, then the end", offsets.size(), (size_t)LLNightSky::CELLS + 1);
        ensure_equals("starts at 0", offsets.front(), 0U);
        ensure_equals("ends at the last star", (size_t)offsets.back(), binned.size());
        ensure("in order", std::is_sorted(offsets.begin(), offsets.end()));
        // the shader stores the offsets as floats: exact below 2^24
        ensure("exact as floats", binned.size() < (1U << 24));
    }

    template<> template<>
    void nightsky_object::test<7>()
    {
        set_test_name("the Pole Star stands above the north at the latitude of Rome");
        for (const LLVector3& sun : { MIDNIGHT, LLVector3(0.f, 0.f, 1.f), LLVector3(1.f, 0.f, 0.f), LLVector3(0.3f, -0.5f, 0.2f) })
        {
            const LLVector3 polaris = region(sun, WINTER, 2.53f, 89.26f);
            const F32 altitude = std::asin(polaris.mV[VZ]) / DEG;
            ensure("altitude " + std::to_string(altitude), std::fabs(altitude - (F32)LLNightSky::LATITUDE) < 1.f);
            ensure("in the north", polaris.mV[VY] > 0.7f && std::fabs(polaris.mV[VX]) < 0.02f);
        }
    }

    template<> template<>
    void nightsky_object::test<8>()
    {
        set_test_name("Orion is up on winter nights and down on summer nights");
        // Betelgeuse (RA 5h55m, Dec +7.4°)
        const LLVector3 winter = region(MIDNIGHT, WINTER, 5.92f, 7.41f);
        const LLVector3 summer = region(MIDNIGHT, SUMMER, 5.92f, 7.41f);
        ensure("up in winter", winter.mV[VZ] > 0.5f);
        ensure("in the south", winter.mV[VY] < 0.f);
        ensure("down in summer", summer.mV[VZ] < 0.f);
        // Vega (RA 18h37m, Dec +38.8°) the other way round
        ensure("Vega up in summer", region(MIDNIGHT, SUMMER, 18.62f, 38.78f).mV[VZ] > 0.7f);
    }

    template<> template<>
    void nightsky_object::test<9>()
    {
        set_test_name("stars rise in the east as the region's night goes on");
        LLVector3 sunset(-1.f, 0.f, -0.05f);
        sunset.normVec();
        const LLVector3 early = region(sunset, WINTER, 5.92f, 7.41f);
        const LLVector3 late = region(MIDNIGHT, WINTER, 5.92f, 7.41f);
        ensure("in the east at dusk", early.mV[VX] > 0.5f);
        ensure("higher at midnight", late.mV[VZ] > early.mV[VZ]);
    }

    template<> template<>
    void nightsky_object::test<10>()
    {
        set_test_name("the rotation keeps directions unit and right-handed");
        LLVector3 sun(0.2f, 0.4f, -0.9f);
        sun.normVec();
        LLVector3 axes[3];
        LLNightSky::axes(sun, SUMMER, axes);
        ensure("unit x", std::fabs(axes[0].length() - 1.f) < 1e-5f);
        ensure("unit y", std::fabs(axes[1].length() - 1.f) < 1e-5f);
        ensure("orthogonal", std::fabs(axes[0] * axes[1]) < 1e-5f);
        ensure("right-handed", dist_vec(axes[0] % axes[1], axes[2]) < 1e-5f);
    }

    template<> template<>
    void nightsky_object::test<11>()
    {
        set_test_name("the Milky Way is bright in Sagittarius and dark in the rift");
        const std::vector<U32> map = milky_way();
        ensure_equals("2048 x 512 texels", map.size(),
                      (size_t)LLNightSky::MILKY_WAY_WIDTH * LLNightSky::MILKY_WAY_HEIGHT);
        // the star clouds of Sagittarius (within 8 degrees of the centre,
        // south of the plane, between its dust lanes) are the brightest of
        // the sky: brighter than Crux's band (l 296), far brighter than the
        // faint band towards the anticentre (Auriga)
        F32 sagittarius = 0.f;
        for (S32 l = -8; l <= 8; l += 2)
        {
            for (S32 b = -10; b <= 2; b += 2)
            {
                sagittarius = std::max(sagittarius, glow(map, (F32)l, (F32)b));
            }
        }
        ensure("brighter than Crux: " + std::to_string(sagittarius), sagittarius > glow(map, -64.f, -1.f));
        ensure("4 times the anticentre: " + std::to_string(sagittarius), sagittarius > 4.f * glow(map, 178.f, 0.f));
        // far from the plane: nothing
        ensure("nothing far from the plane", glow(map, 0.f, -42.f) < 0.02f);
        // the Great Rift in Aquila (l 30, b 3) is far darker than the band
        // just south of it (b -4), and the Coalsack (l 301, b -1) than the
        // band beside it in Crux (l 296)
        ensure("the Aquila Rift", glow(map, 30.f, 3.f) * 3.f < glow(map, 30.f, -4.f));
        ensure("the Coalsack", glow(map, -59.f, -1.f) * 2.f < glow(map, -64.f, -1.f));
        // the faint stars' colour: dust reddens the centre
        const U32 centre = map[map_y(-6.f) * LLNightSky::MILKY_WAY_WIDTH + map_x(2.f)];
        const F32 bv = (F32)((centre >> 16) & 255) / 100.f;
        ensure("colour " + std::to_string(bv), bv >= 0.2f && bv < 1.6f);
        // the grain follows the stars: more in the band than above it
        auto grain = [&](F32 l, F32 b) { return (map[map_y(b) * LLNightSky::MILKY_WAY_WIDTH + map_x(l)] >> 8) & 255; };
        ensure("grain in the band", grain(-75.f, 0.f) > grain(-75.f, 30.f));
    }

    template<> template<>
    void nightsky_object::test<12>()
    {
        set_test_name("a damaged Milky Way map is refused");
        const std::vector<U8> map = read_source("../app_settings/stars/milky_way.bin");
        ensure("the map parses", map_parses(map));
        ensure("header only", !map_parses(std::vector<U8>(map.begin(), map.begin() + 8)));
        std::vector<U8> aurora = map;
        const char other[] = "AURMWAY2";
        std::copy(other, other + 8, aurora.begin());
        ensure("Aurora's magic", !map_parses(aurora));
        std::vector<U8> wrong = map;
        wrong[8] ^= 1; // another width than the shader's
        ensure("another width", !map_parses(wrong));
        std::vector<U8> huge = map;
        huge[23] |= 0x80; // a colour grid wider than the map: refused before any allocation
        ensure("a huge colour grid", !map_parses(huge));
        std::vector<U8> empty_grid = map;
        empty_grid[20] = empty_grid[21] = empty_grid[22] = empty_grid[23] = 0;
        ensure("an empty colour grid", !map_parses(empty_grid));
        ensure("truncated", !map_parses(std::vector<U8>(map.begin(), map.end() - 100)));
        std::vector<U8> damaged = map;
        damaged[200] ^= 0xff;
        ensure("damaged stream", !map_parses(damaged));
    }

    template<> template<>
    void nightsky_object::test<13>()
    {
        set_test_name("the shader reads the map at its size");
        const std::vector<U8> bytes = read_source("../app_settings/shaders/class1/deferred/skyF.glsl");
        const std::string sky(bytes.begin(), bytes.end());
        auto value = [&](const std::string& name)
        {
            const std::string key = "#define " + name + " ";
            const size_t at = sky.find(key);
            ensure(name + " defined", at != std::string::npos);
            return std::strtod(sky.c_str() + at + key.size(), nullptr);
        };
        ensure_equals("NIGHT_SKY_MW_WIDTH", value("NIGHT_SKY_MW_WIDTH"), (F64)LLNightSky::MILKY_WAY_WIDTH);
        ensure_equals("NIGHT_SKY_MW_HEIGHT", value("NIGHT_SKY_MW_HEIGHT"), (F64)LLNightSky::MILKY_WAY_HEIGHT);
        const F64 reach = value("NIGHT_SKY_MW_REACH");
        ensure("NIGHT_SKY_MW_REACH " + std::to_string(reach),
               std::fabs(reach - LLNightSky::MILKY_WAY_REACH * 3.14159265358979 / 180.0) < 1e-6);
    }

    template<> template<>
    void nightsky_object::test<14>()
    {
        set_test_name("the stars too bright for Tycho-2 keep their colour");
        const std::vector<LLNightSky::CatalogueStar> stars = catalogue();
        // from Hipparcos: Arcturus orange, once, and Capella yellow
        auto stars_near = [&](F32 ra_deg, F32 dec_deg)
        {
            const LLVector3 d = LLNightSky::equatorial(ra_deg * DEG, dec_deg * DEG);
            std::vector<LLNightSky::CatalogueStar> out;
            for (const LLNightSky::CatalogueStar& s : stars)
            {
                if (s.magnitude < 3.f && LLNightSky::equatorial(s.ra, s.dec) * d > 0.9999f)
                {
                    out.push_back(s);
                }
            }
            return out;
        };
        const std::vector<LLNightSky::CatalogueStar> arcturus = stars_near(213.92f, 19.18f);
        ensure_equals("Arcturus listed once", arcturus.size(), (size_t)1);
        ensure("Arcturus orange", arcturus[0].has_bv && arcturus[0].bv > 1.1f);
        const std::vector<LLNightSky::CatalogueStar> capella = stars_near(79.17f, 46.f);
        ensure("Capella listed", !capella.empty());
        for (const LLNightSky::CatalogueStar& s : capella)
        {
            ensure("Capella yellow", s.has_bv && s.bv > 0.6f);
        }
    }

    template<> template<>
    void nightsky_object::test<15>()
    {
        set_test_name("Greenwich sidereal time starts from its J2000 value");
        // 18h 41m 50.5s at 2000-01-01 12:00 UT
        ensure("at J2000", std::fabs(LLNightSky::greenwichSidereal(0.0) - 280.4606) < 1e-3);
        // a sidereal day later it is back, a solar day later 3m 56s ahead
        const F64 sidereal_day = 360.0 / 360.98564736629;
        ensure("a sidereal day later", std::fabs(LLNightSky::greenwichSidereal(sidereal_day) - 280.4606) < 1e-3);
        const F64 ahead = std::fmod(LLNightSky::greenwichSidereal(1.0) - LLNightSky::greenwichSidereal(0.0) + 360.0, 360.0);
        ensure("a solar day later: " + std::to_string(ahead), std::fabs(ahead - 0.9856) < 1e-3);
        // within 0 to 360 before J2000 too
        const F64 before = LLNightSky::greenwichSidereal(-1000.25);
        ensure("in range", before >= 0.0 && before < 360.0);
    }

    template<> template<>
    void nightsky_object::test<16>()
    {
        set_test_name("in real time a place further west sees the sky later");
        constexpr F64 PARIS = 2.3522;
        constexpr F64 NEW_YORK = -74.006;
        const LLNightSky::StarHour real = LLNightSky::StarHour::RealTime;
        // the same instant: Betelgeuse stands elsewhere
        const LLVector3 p = region(MIDNIGHT, WINTER, 5.92f, 7.41f, real, PARIS);
        const LLVector3 n = region(MIDNIGHT, WINTER, 5.92f, 7.41f, real, NEW_YORK);
        ensure("elsewhere at the same instant", dist_vec(p, n) > 0.5f);
        // New York, later by the longitude difference in sidereal time
        const F64 later = (PARIS - NEW_YORK) / 360.98564736629;
        LLVector3 paris[3];
        LLVector3 new_york[3];
        LLNightSky::axes(MIDNIGHT, WINTER, paris, real, PARIS);
        LLNightSky::axes(MIDNIGHT, WINTER + later, new_york, real, NEW_YORK);
        for (S32 i = 0; i < 3; ++i)
        {
            ensure("axis " + std::to_string(i), dist_vec(paris[i], new_york[i]) < 1e-4f);
        }
        // the Pole Star still at the latitude, whatever the hour
        const F32 polaris = std::asin(region(MIDNIGHT, WINTER + later, 2.53f, 89.26f, real, NEW_YORK).mV[VZ]) / DEG;
        ensure("Polaris " + std::to_string(polaris), std::fabs(polaris - (F32)LLNightSky::LATITUDE) < 1.f);
        // the clock turns the sky: a quarter of a sidereal day later,
        // Betelgeuse has moved
        const LLVector3 q = region(MIDNIGHT, WINTER + 0.25 * 360.0 / 360.98564736629, 5.92f, 7.41f, real, PARIS);
        ensure("turns with the clock", dist_vec(p, q) > 0.5f);
    }

    template<> template<>
    void nightsky_object::test<17>()
    {
        set_test_name("with the region's sun the longitude changes nothing");
        const LLNightSky::StarHour sun = LLNightSky::StarHour::RegionSun;
        LLVector3 a[3];
        LLVector3 b[3];
        LLNightSky::axes(MIDNIGHT, WINTER, a, sun, 2.35);
        LLNightSky::axes(MIDNIGHT, WINTER, b, sun, -74.0);
        // the default is the region's sun
        LLVector3 c[3];
        LLNightSky::axes(MIDNIGHT, WINTER, c);
        for (S32 i = 0; i < 3; ++i)
        {
            ensure("axis " + std::to_string(i), a[i] == b[i]);
            ensure("default axis " + std::to_string(i), a[i] == c[i]);
        }
    }

    template<> template<>
    void nightsky_object::test<18>()
    {
        set_test_name("every preset has its own latitude and longitude, Custom none");
        const std::vector<LLNightSky::SkyPlace> places = all_places();
        ensure_equals("places", places.size(), (size_t)12);
        ensure("Rome first", LLNightSky::skyPlace(0) == LLNightSky::SkyPlace::Rome);
        ensure("Custom last", places.back() == LLNightSky::SkyPlace::Custom);
        ensure("out of range: Rome", LLNightSky::skyPlace(LLNightSky::SKY_PLACES) == LLNightSky::SkyPlace::Rome);
        ensure("far out of range: Rome", LLNightSky::skyPlace(0xffffffffU) == LLNightSky::SkyPlace::Rome);
        std::vector<std::pair<F64, F64>> seen;
        for (LLNightSky::SkyPlace place : places)
        {
            F64 latitude = 1234.0;
            F64 longitude = 1234.0;
            const bool has_latitude = LLNightSky::placeLatitude(place, latitude);
            const bool has_longitude = LLNightSky::placeLongitude(place, longitude);
            const std::string name = "place " + std::to_string((U32)place);
            ensure_equals(name + " both or none", has_latitude, has_longitude);
            if (place == LLNightSky::SkyPlace::Custom)
            {
                ensure(name + " none", !has_latitude && latitude == 1234.0 && longitude == 1234.0);
                continue;
            }
            ensure(name + " has its place", has_latitude);
            ensure(name + " latitude in range", latitude >= -90.0 && latitude <= 90.0);
            ensure(name + " longitude in range", longitude >= -180.0 && longitude <= 180.0);
            ensure(name + " its own", std::find(seen.begin(), seen.end(), std::make_pair(latitude, longitude)) == seen.end());
            seen.emplace_back(latitude, longitude);
        }
        F64 v = 0.0;
        ensure("Rome's latitude", LLNightSky::placeLatitude(LLNightSky::SkyPlace::Rome, v) && v == LLNightSky::LATITUDE);
        ensure("Rome's longitude", LLNightSky::placeLongitude(LLNightSky::SkyPlace::Rome, v) && v == LLNightSky::LONGITUDE);
        ensure("New York's longitude", LLNightSky::placeLongitude(LLNightSky::SkyPlace::NewYork, v) && v == -74.006);
        ensure("Sydney in the south", LLNightSky::placeLatitude(LLNightSky::SkyPlace::Sydney, v) && v < -33.0);
    }

    template<> template<>
    void nightsky_object::test<19>()
    {
        set_test_name("each place sees its own sky");
        // seen from any place, the celestial pole stands at its latitude:
        // the Pole Star above the north in the north, under the horizon in
        // the south
        for (LLNightSky::SkyPlace place : all_places())
        {
            F64 latitude = 0.0;
            if (!LLNightSky::placeLatitude(place, latitude))
            {
                continue;
            }
            const LLVector3 polaris = region_at(observer_at(latitude), MIDNIGHT, WINTER, 2.53f, 89.26f);
            const F32 altitude = std::asin(llclamp(polaris.mV[VZ], -1.f, 1.f)) / DEG;
            ensure("place " + std::to_string((U32)place) + " altitude " + std::to_string(altitude),
                   std::fabs(altitude - (F32)latitude) < 1.f);
        }
        // Acrux (RA 12h27m, Dec -63.1 degrees): never above Rome, high over
        // Sydney at some hour of the night
        auto highest = [](F64 latitude)
        {
            F32 best = -2.f;
            for (S32 h = 0; h < 24; ++h)
            {
                const F32 a = (F32)h / 24.f * 2.f * 3.14159265f;
                LLVector3 sun(std::cos(a) * 0.3f, std::sin(a) * 0.3f, -0.95f);
                sun.normVec();
                best = llmax(best, region_at(observer_at(latitude), sun, WINTER, 12.44f, -63.1f).mV[VZ]);
            }
            return best;
        };
        F64 sydney = 0.0;
        LLNightSky::placeLatitude(LLNightSky::SkyPlace::Sydney, sydney);
        ensure("Acrux never above Rome", highest(LLNightSky::LATITUDE) < 0.f);
        ensure("Acrux high over Sydney", highest(sydney) > 0.5f);
    }

    template<> template<>
    void nightsky_object::test<20>()
    {
        set_test_name("the poles give a finite rotation");
        for (F64 latitude : { 90.0, -90.0, 89.9999, 120.0, -1000.0 })
        {
            for (const LLVector3& sun : { MIDNIGHT, LLVector3(0.f, 0.f, 1.f), LLVector3(0.3f, -0.5f, 0.2f) })
            {
                for (LLNightSky::StarHour hour : { LLNightSky::StarHour::RegionSun, LLNightSky::StarHour::RealTime })
                {
                    LLVector3 axes[3];
                    LLNightSky::axes(sun, WINTER, axes, observer_at(latitude, 0.0, hour));
                    for (S32 i = 0; i < 3; ++i)
                    {
                        const std::string name = "latitude " + std::to_string(latitude) + " axis " + std::to_string(i);
                        ensure(name + " finite", axes[i].isFinite());
                        ensure(name + " unit", std::fabs(axes[i].length() - 1.f) < 1e-4f);
                    }
                }
            }
        }
        // at the North Pole the Pole Star is overhead; beyond it, clamped
        const LLVector3 polaris = region_at(observer_at(90.0), MIDNIGHT, WINTER, 2.53f, 89.26f);
        ensure("overhead", polaris.mV[VZ] > 0.999f);
        const LLVector3 beyond = region_at(observer_at(120.0), MIDNIGHT, WINTER, 2.53f, 89.26f);
        ensure("clamped to the pole", dist_vec(polaris, beyond) < 1e-5f);
        // a latitude that is no number: Rome's
        const LLVector3 rome = region_at(observer_at(LLNightSky::LATITUDE), MIDNIGHT, WINTER, 5.92f, 7.41f);
        const LLVector3 nan = region_at(observer_at(std::nan("")), MIDNIGHT, WINTER, 5.92f, 7.41f);
        ensure("non-finite latitude: Rome's", dist_vec(rome, nan) < 1e-6f);
    }

    template<> template<>
    void nightsky_object::test<21>()
    {
        set_test_name("a preset's place, or the Custom one clamped and Rome's when not a number");
        const LLNightSky::StarHour real = LLNightSky::StarHour::RealTime;
        // a preset ignores the typed values
        const LLNightSky::Observer tokyo = LLNightSky::observer(LLNightSky::SkyPlace::Tokyo, 10.0, 20.0, real);
        ensure("Tokyo latitude", tokyo.latitude == 35.676);
        ensure("Tokyo longitude", tokyo.longitude == 139.650);
        ensure("the hour kept", tokyo.hour == real);
        const LLNightSky::Observer rome = LLNightSky::observer(LLNightSky::skyPlace(0), 10.0, 20.0, LLNightSky::StarHour::RegionSun);
        ensure("Rome by default", rome.latitude == LLNightSky::LATITUDE && rome.longitude == LLNightSky::LONGITUDE);
        ensure("region's sun kept", rome.hour == LLNightSky::StarHour::RegionSun);
        // Custom keeps the typed values within range
        const LLNightSky::SkyPlace custom = LLNightSky::SkyPlace::Custom;
        LLNightSky::Observer at = LLNightSky::observer(custom, -45.5, 170.25, real);
        ensure("custom latitude", at.latitude == -45.5);
        ensure("custom longitude", at.longitude == 170.25);
        at = LLNightSky::observer(custom, 95.0, -200.0, real);
        ensure("latitude clamped", at.latitude == 90.0);
        ensure("longitude clamped", at.longitude == -180.0);
        at = LLNightSky::observer(custom, -1e9, 1e9, real);
        ensure("clamped below and above", at.latitude == -90.0 && at.longitude == 180.0);
        const F64 inf = std::numeric_limits<F64>::infinity();
        at = LLNightSky::observer(custom, std::nan(""), inf, real);
        ensure("non-finite latitude: Rome's", at.latitude == LLNightSky::LATITUDE);
        ensure("non-finite longitude: Rome's", at.longitude == LLNightSky::LONGITUDE);
        at = LLNightSky::observer(custom, -inf, std::nan(""), real);
        ensure("non-finite again: Rome's", at.latitude == LLNightSky::LATITUDE && at.longitude == LLNightSky::LONGITUDE);
        // the older entry point is Rome's latitude
        LLVector3 a[3];
        LLVector3 b[3];
        LLNightSky::axes(MIDNIGHT, WINTER, a, real, -74.006);
        LLNightSky::axes(MIDNIGHT, WINTER, b, observer_at(LLNightSky::LATITUDE, -74.006, real));
        for (S32 i = 0; i < 3; ++i)
        {
            ensure("same as Rome's observer, axis " + std::to_string(i), a[i] == b[i]);
        }
    }

    template<> template<>
    void nightsky_object::test<22>()
    {
        set_test_name("an observer's longitude counts with the real time only");
        // with the region's sun the longitude changes nothing
        LLVector3 a[3];
        LLVector3 b[3];
        LLNightSky::axes(MIDNIGHT, WINTER, a, observer_at(45.0, 2.35));
        LLNightSky::axes(MIDNIGHT, WINTER, b, observer_at(45.0, -74.0));
        for (S32 i = 0; i < 3; ++i)
        {
            ensure("region's sun, axis " + std::to_string(i), a[i] == b[i]);
        }
        // in real time New York sees the sky Paris had, later by the
        // longitude difference in sidereal time
        F64 paris_east = 0.0;
        F64 new_york_east = 0.0;
        LLNightSky::placeLongitude(LLNightSky::SkyPlace::Paris, paris_east);
        LLNightSky::placeLongitude(LLNightSky::SkyPlace::NewYork, new_york_east);
        const LLNightSky::StarHour real = LLNightSky::StarHour::RealTime;
        const LLNightSky::Observer paris = observer_at(45.0, paris_east, real);
        const LLNightSky::Observer new_york = observer_at(45.0, new_york_east, real);
        ensure("elsewhere at the same instant",
               dist_vec(region_at(paris, MIDNIGHT, WINTER, 5.92f, 7.41f), region_at(new_york, MIDNIGHT, WINTER, 5.92f, 7.41f)) > 0.5f);
        const F64 later = (paris_east - new_york_east) / 360.98564736629;
        LLNightSky::axes(MIDNIGHT, WINTER, a, paris);
        LLNightSky::axes(MIDNIGHT, WINTER + later, b, new_york);
        for (S32 i = 0; i < 3; ++i)
        {
            ensure("later, axis " + std::to_string(i), dist_vec(a[i], b[i]) < 1e-4f);
        }
        // the Pole Star still at the latitude, whatever the hour
        const F32 polaris = std::asin(region_at(new_york, MIDNIGHT, WINTER + later, 2.53f, 89.26f).mV[VZ]) / DEG;
        ensure("Polaris " + std::to_string(polaris), std::fabs(polaris - 45.f) < 1.f);
    }
}
