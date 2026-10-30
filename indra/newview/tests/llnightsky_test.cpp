/**
 * @file llnightsky_test.cpp
 * @brief The real night sky's catalogue, binning and frame
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

#include "../llviewerprecompiledheaders.h"
#include "../llnightsky.h"
#include "../test/lltut.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
    constexpr F32 DEG = 3.14159265f / 180.f;

    // app_settings/stars/stars.bin, found from this source file
    std::vector<U8> read_catalogue()
    {
        std::string here = __FILE__;
        const size_t slash = here.find_last_of("/\\");
        const std::string path = here.substr(0, slash + 1) + "../app_settings/stars/stars.bin";
        std::ifstream file(path, std::ios::binary);
        tut::ensure("open " + path, file.is_open());
        return std::vector<U8>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
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
    LLVector3 region(const LLVector3& sun, F64 unix_days, F32 ra_hours, F32 dec_deg)
    {
        LLVector3 axes[3];
        LLNightSky::axes(sun, unix_days, axes);
        const LLVector3 e = LLNightSky::equatorial(ra_hours * 15.f * DEG, dec_deg * DEG);
        return axes[0] * e.mV[VX] + axes[1] * e.mV[VY] + axes[2] * e.mV[VZ];
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
}
