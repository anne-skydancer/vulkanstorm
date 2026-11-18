/**
 * @file llpacketbuffer.h
 * @brief definition of LLPacketBuffer class for implementing a
 * resend, drop, or delay in packet transmissions.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#ifndef LL_LLPACKETBUFFER_H
#define LL_LLPACKETBUFFER_H

#include "net.h"        // for NET_BUFFER_SIZE
#include "llhost.h"

#include <cstring>

class LLPacketBuffer
{
public:
    LLPacketBuffer(const LLHost &host, const char *datap, const S32 size);
    LLPacketBuffer(S32 hSocket);    // receive a packet
    ~LLPacketBuffer();

    // <Vulkanstorm> The buffer is NET_BUFFER_SIZE (8 KB) but packets are mostly well under
    // 1 KB: copy only the mSize bytes in use (the ring copies one per push and per pop).
    LLPacketBuffer(const LLPacketBuffer& other)
    {
        copyFrom(other);
    }
    LLPacketBuffer& operator=(const LLPacketBuffer& other)
    {
        if (this != &other)
        {
            copyFrom(other);
        }
        return *this;
    }
    // </Vulkanstorm>

    S32         getSize() const                 { return mSize; }
    const char  *getData() const                { return mData; }
    LLHost      getHost() const                 { return mHost; }
    LLHost      getReceivingInterface() const   { return mReceivingIF; }

    void init(S32 hSocket);
    void init(const char* buffer, S32 data_size, const LLHost& host);

    // Whether LLCircuitData::checkPacketInID() has already been run for this
    // packet (done at socket-read time, before this packet was sorted into
    // the high/low priority inbound queue).
    bool    getPacketIDChecked() const          { return mPacketIDChecked; }
    void    setPacketIDChecked(bool checked)    { mPacketIDChecked = checked; }

protected:
    // <Vulkanstorm>
    void copyFrom(const LLPacketBuffer& other)
    {
        // mSize is 0 or <= NET_BUFFER_SIZE (init() rejects larger packets); a failed socket
        // receive can leave it negative.
        if (other.mSize > 0)
        {
            memcpy(mData, other.mData, other.mSize);
        }
        else
        {
            mData[0] = other.mData[0];
        }
        mSize = other.mSize;
        mHost = other.mHost;
        mReceivingIF = other.mReceivingIF;
        mPacketIDChecked = other.mPacketIDChecked;
    }
    // </Vulkanstorm>

    char    mData[NET_BUFFER_SIZE]; // packet data       /* Flawfinder : ignore */
    S32     mSize;                  // size of buffer in bytes
    LLHost  mHost;                  // source/dest IP and port
    LLHost  mReceivingIF;           // source/dest IP and port
    bool    mPacketIDChecked = false;
};

#endif


