/**
 * @file llj2ccommon.h
 * @brief What the two J2C implementations of the viewer share, codec-independent: the texture comment scan,
 *        the encoder's layer and resolution policy.
 *
 * Moved out of llimagej2coj.cpp (LLImageJ2COJ, OpenJPEG) so that llimagej2cneo.cpp (LLImageJ2CNeo, neo-j2k,
 * USE_NEOJ2K) uses the same code; both include it. The code keeps its origin and its attributions.
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLJ2CCOMMON_H
#define LL_LLJ2CCOMMON_H

#include "llimagej2c.h"

#include <cmath>
#include <string>

const F32 LAST_TCP_RATE = 1.f/DEFAULT_COMPRESSION_RATE; // should be 8, giving a 1:8 ratio

namespace LLJ2CCommon
{
    /**
     * Estimates the number of layers necessary depending on the image surface (w x h)
     */
    inline U32 estimate_num_layers(U32 surface)
    {
        if      (surface <= 1024)    return 2;  // Tiny (<=32x32)
        else if (surface <= 16384)   return 3;  // Small (<=128x128)
        else if (surface <= 262144)  return 4;  // Medium (<=512x512)
        else if (surface <= 1048576) return 5;  // Up to ~1MP
        else                         return 6;  // Up to ~1.5-2MP
    }

    /**
     * Sets the parameters.tcp_rates according to the number of layers and a last tcp_rate value (which equals to the final compression ratio).
     *
     * Example for 6 layers:
     *
     *  i = 5, parameters.tcp_rates[6 - 1 - 5] = 8.0f * (1 << (5 << 1)) = 8192  // Layer 5 (lowest quality)
     *  i = 4, parameters.tcp_rates[6 - 1 - 4] = 8.0f * (1 << (4 << 1)) = 2048  // Layer 4
     *  i = 3, parameters.tcp_rates[6 - 1 - 3] = 8.0f * (1 << (3 << 1)) =  512  // Layer 3
     *  i = 2, parameters.tcp_rates[6 - 1 - 2] = 8.0f * (1 << (2 << 1)) =  128  // Layer 2
     *  i = 1, parameters.tcp_rates[6 - 1 - 1] = 8.0f * (1 << (1 << 1)) =   32  // Layer 1
     *  i = 0, parameters.tcp_rates[6 - 1 - 0] = 8.0f * (1 << (0 << 1)) =    8  // Layer 0 (highest quality)
     *
     * Any parameters structure with tcp_numlayers and tcp_rates (opj_cparameters_t, nj2k_cparameters_t).
     */
    template<class Parameters>
    void set_tcp_rates(Parameters* parameters, U32 num_layers = 1, F32 last_tcp_rate = LAST_TCP_RATE)
    {
        parameters->tcp_numlayers = num_layers;

        for (int i = num_layers - 1; i >= 0; i--)
        {
            parameters->tcp_rates[num_layers - 1 - i] = last_tcp_rate * static_cast<F32>(1 << (i << 1));
        }
    }

    // The tiling the encoder's guess of the output size and its resolutions follow from the image's size.
    struct Tiling
    {
        U32 width_tiles;
        U32 height_tiles;
        int numresolution; // 0: the encoder's default stays
    };

    inline Tiling tiling(S32 width, S32 height)
    {
        Tiling t;
        t.width_tiles = (width >> 6);
        t.height_tiles = (height >> 6);
        t.numresolution = 0;

        // Allow images with a width or height that are MIN_IMAGE_SIZE <= x < 64
        if (t.width_tiles == 0 && (width >= MIN_IMAGE_SIZE))
        {
            t.width_tiles = 1;
        }
        if (t.height_tiles == 0 && (height >= MIN_IMAGE_SIZE))
        {
            t.height_tiles = 1;
        }

        if (t.width_tiles == 1 || t.height_tiles == 1)
        {
            // Images with either dimension less than 32 need less number of resolutions otherwise they error
            int min_dim = width < height ? width : height;
            t.numresolution = 1 + (int)floor(log2(min_dim));
        }
        return t;
    }

    // <FS:Techwolf Lupindo> texture comment metadata reader
    // The comment of a codestream (a COM marker in its first 1024 bytes), when it passes the sanity checks;
    // false (and `comment` untouched) otherwise.
    inline bool scanComment(const U8* c_data, S32 c_size, std::string& comment)
    {
        S32 position = 0;

        while (position < 1024 && position < (c_size - 7)) // the comment field should be in the first 1024 bytes.
        {
            if (c_data[position] == 0xff && c_data[position + 1] == 0x64)
            {
                U8 high_byte = c_data[position + 2];
                U8 low_byte = c_data[position + 3];
                S32 c_length = (high_byte * 256) + low_byte; // This size also counts the markers, 00 01 and itself
                if (c_length > 200) // sanity check
                {
                    // While comments can be very long, anything longer then 200 is suspect.
                    break;
                }

                if (c_length < 4)
                {
                    // too short to hold the length field and the 00 01 marker; c_length - 4 below would wrap.
                    break;
                }

                if (position + 2 + c_length > c_size)
                {
                    // comment extends past end of data, corruption, or all data not retrived yet.
                    break;
                }

                // if the comment block does not end at the end of data, check to see if the next
                // block starts with 0xFF
                if (position + 2 + c_length < c_size && c_data[position + 2 + c_length] != 0xff)
                {
                    // invalied comment block
                    break;
                }

                // extract the comment minus the markers, 00 01
                comment.assign((const char*)(c_data + position + 6), c_length - 4);
                return true;
            }
            position++;
        }
        return false;
    }
    // </FS:Techwolf Lupindo>
}

#endif // LL_LLJ2CCOMMON_H
