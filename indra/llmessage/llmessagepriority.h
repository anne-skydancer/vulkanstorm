/**
 * @file llmessagepriority.h
 * @brief Classification of inbound UDP packets for the high/low priority queues
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// <Vulkanstorm> Free of viewer headers so that a standalone test can include it.

#ifndef LL_LLMESSAGEPRIORITY_H
#define LL_LLMESSAGEPRIORITY_H

#include <cstdint>

namespace LLMessagePriority
{
// Mirrors LL_PACKET_ID_SIZE and LL_ZERO_CODE_FLAG in message.h (checked by static_asserts there).
constexpr int     PACKET_ID_SIZE = 6;
constexpr uint8_t ZERO_CODE_FLAG = 0x80;

// True for every message that should be handled in the high priority queue: all except the
// low-frequency AvatarAppearance message ("Low 158": message number bytes 0xFF 0xFF 0x00 0x9E),
// which must not overtake the first ObjectUpdate of its agent (LL #6017).
//
// 'pkt' is the raw packet as received, before zeroCodeExpand(): byte 0 is the flags byte, bytes
// 1-4 the sequence number, byte 5 the extra header length (sims send 0; the extra bytes come
// *after* the message number, so the number always starts at byte PACKET_ID_SIZE). Zero-coding
// applies only to what follows the 6 byte packet id (zeroCodeExpand copies it verbatim), and
// encodes a run of N zero bytes as 0x00 N, so the single zero byte of 0x00 0x9E becomes
// 0x00 0x01 and AvatarAppearance arrives as FF FF 00 01 9E when the zero-code flag is set.
inline bool isHighPriority(const uint8_t* pkt, int size)
{
    // avoid stepping out of bounds: id + 4 bytes covers a plain low-frequency message number
    if (size < PACKET_ID_SIZE + 4)
    {
        return false;
    }

    const uint8_t* header = pkt + PACKET_ID_SIZE;
    if (header[0] != 255)
    {
        // high-frequency message (single byte number): low priority
        return false;
    }

    // all medium- and low-frequency messages are high priority except AvatarAppearance
    if (header[1] != 255)
    {
        return true; // medium frequency
    }
    if (pkt[0] & ZERO_CODE_FLAG)
    {
        // coded: FF FF 00 01 9E; needs the extra byte
        if (size < PACKET_ID_SIZE + 5)
        {
            return true;
        }
        return !(header[2] == 0 && header[3] == 1 && header[4] == 158);
    }
    return !(header[2] == 0 && header[3] == 158);
}
}

#endif
