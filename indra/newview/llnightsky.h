/**
 * @file llnightsky.h
 * @brief The real night sky: the naked-eye stars and the Milky Way over Rome
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

#ifndef LL_NIGHTSKY_H
#define LL_NIGHTSKY_H

#include "stdtypes.h"
#include "v3math.h"

#include <string>
#include <vector>

// The real night sky, drawn by deferred/skyF.glsl (Preferences > Graphics >
// Rendering 2 > Night sky). Firestorm's random stars (LLVOWLSky) stay the
// other choice and are drawn as before.
//
// The stars are those of app_settings/stars/stars.bin (HYG database,
// CC BY-SA 4.0, magnitude 6.5 and brighter) in J2000 equatorial
// coordinates. They are sorted into the cells of a cube map (FACE_CELLS per
// face edge); a star is also listed in the neighbouring cells its point can
// cover (MARGIN), so a sky pixel only reads the stars of its own cell.
//
// The sky turns around the celestial pole as seen from Rome: the hour
// follows the region's sun, so the stars turn once per region day, and
// which stars are up at night follows the season of today's date.
//
// Frames: the region's is x east, y north, z up. The catalogue's is x
// towards the March equinox, z towards the north celestial pole.
//
// GL-free, so that the integration test can run it (tests/llnightsky_test.cpp).
namespace LLNightSky
{
    // Cells per edge of a cube face. Must match NIGHT_SKY_FACE_CELLS (skyF.glsl).
    constexpr U32 FACE_CELLS = 24;
    constexpr U32 CELLS = 6 * FACE_CELLS * FACE_CELLS;
    // Angular radius (radians) of the widest star point the shader draws: a
    // star is also listed in every cell within it. Must match NIGHT_SKY_MARGIN.
    constexpr F32 MARGIN = 0.021f;
    // Latitude of Rome (degrees north).
    constexpr F64 LATITUDE = 41.9028;

    // A star of the table, decoded.
    struct CatalogueStar
    {
        F32 ra;         // right ascension (radians, J2000)
        F32 dec;        // declination (radians, J2000)
        F32 magnitude;  // visual magnitude
        F32 bv;         // B-V colour index, when has_bv
        bool has_bv;
    };

    // One star as the shader reads it: two RGBA32F texels.
    struct GpuStar
    {
        F32 pos[4];     // unit direction (catalogue frame), peak intensity (linear)
        F32 color[4];   // linear colour (luminance 1), point size (pixels, sigma)

        bool operator==(const GpuStar& other) const;
    };

    // Decodes stars.bin (format in scripts/generate-star-catalog.py).
    // Returns false and sets error when the table is damaged.
    bool parse(const U8* data, size_t size, std::vector<CatalogueStar>& stars, std::string& error);

    // Unit vector of an equatorial position.
    LLVector3 equatorial(F32 ra, F32 dec);

    // Peak intensity (linear) of a star of this visual magnitude. The real
    // range (Sirius is 1,500 times brighter than a magnitude 6.5 star) is
    // compressed to about 1 to 100, so that the faintest stars just show over
    // the night sky and the constellations' bright stars stand out, as the
    // eye sees them.
    F32 intensity(F32 magnitude);

    // Size of a star's point in pixels (sigma of its Gaussian): the
    // brightest stars are a little larger.
    F32 pointSize(F32 magnitude);

    // Colour (linear, luminance 1) of a star of this B-V index.
    LLVector3 colour(bool has_bv, F32 bv);

    // The stars as the shader reads them, in catalogue order.
    std::vector<GpuStar> gpuStars(const std::vector<CatalogueStar>& stars);

    // Cell of a direction (any length). Mirrors night_sky_cell in skyF.glsl.
    U32 cellOf(const LLVector3& dir);

    // Sorts the stars into their cells: offsets (CELLS + 1) into binned,
    // where each star is repeated in every cell it reaches.
    void bin(const std::vector<GpuStar>& stars, std::vector<U32>& offsets, std::vector<GpuStar>& binned);

    // Days since 1970-01-01 UTC, now.
    F64 unixDaysNow();

    // Region directions of the catalogue's x, y and z axes (the columns of
    // the rotation from the catalogue's frame to the region's), for the
    // region's sun direction sun (region frame) and the date unix_days.
    void axes(const LLVector3& sun, F64 unix_days, LLVector3 out[3]);
}

#endif // LL_NIGHTSKY_H
