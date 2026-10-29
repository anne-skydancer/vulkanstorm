/**
 * @file lltemplatemessagebuilder.h
 * @brief Declaration of LLTemplateMessageBuilder class.
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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

#ifndef LL_LLTEMPLATEMESSAGEBUILDER_H
#define LL_LLTEMPLATEMESSAGEBUILDER_H

#include <map>

#include "llmessagebuilder.h"
#include "llmsgvariabletype.h"

class LLMsgData;
class LLMessageTemplate;
class LLMsgBlkData;
class LLMessageTemplate;
struct VksMsgBuilder;

// The template message builder. The message data and its packing are the
// Rust crate vkstorm-messages (vkstorm-rust rung three); this class keeps
// the interface, the template lookup and the viewer's error reporting.
class LLTemplateMessageBuilder : public LLMessageBuilder
{
public:

    typedef std::map<const char* , LLMessageTemplate*> message_template_name_map_t;

    LLTemplateMessageBuilder(const message_template_name_map_t&);
    virtual ~LLTemplateMessageBuilder();

    virtual void newMessage(const char* name);

    virtual void nextBlock(const char* blockname);
    virtual bool removeLastBlock(); // TODO: babbage: remove this horror...

    /** All add* methods expect pointers to canonical varname strings. */
    virtual void addBinaryData(const char *varname, const void *data,
                               S32 size);
    virtual void addBOOL(const char* varname, bool b);
    virtual void addS8(const char* varname, S8 s);
    virtual void addU8(const char* varname, U8 u);
    virtual void addS16(const char* varname, S16 i);
    virtual void addU16(const char* varname, U16 i);
    virtual void addF32(const char* varname, F32 f);
    virtual void addS32(const char* varname, S32 s);
    virtual void addU32(const char* varname, U32 u);
    virtual void addU64(const char* varname, U64 lu);
    virtual void addF64(const char* varname, F64 d);
    virtual void addVector3(const char* varname, const LLVector3& vec);
    virtual void addVector4(const char* varname, const LLVector4& vec);
    virtual void addVector3d(const char* varname, const LLVector3d& vec);
    virtual void addQuat(const char* varname, const LLQuaternion& quat);
    virtual void addUUID(const char* varname, const LLUUID& uuid);
    virtual void addIPAddr(const char* varname, const U32 ip);
    virtual void addIPPort(const char* varname, const U16 port);
    virtual void addString(const char* varname, const char* s);
    virtual void addString(const char* varname, const std::string& s);

    virtual bool isMessageFull(const char* blockname) const;
    virtual void compressMessage(U8*& buf_ptr, U32& buffer_length);

    virtual bool isBuilt() const;
    virtual bool isClear() const;
    virtual U32 buildMessage(U8* buffer, U32 buffer_size, U8 offset_to_data);
        /**< Return built message size */

    virtual void clearMessage();

    // TODO: babbage: remove this horror.
    virtual void setBuilt(bool b);

    virtual S32 getMessageSize();
    virtual const char* getMessageName() const;

    virtual void copyFromMessageData(const LLMsgData& data);
    virtual void copyFromLLSD(const LLSD&);

    /** The message built so far as LLMsgData, assembled on request; valid
     *  until the next call or the next change to the message. */
    LLMsgData* getCurrentMessage() const;
private:
    void addData(const char* varname, const void* data,
                     EMsgVariableType type, S32 size);

    VksMsgBuilder* mBuilder;
    mutable LLMsgData* mCurrentSMessageData;
    const LLMessageTemplate* mCurrentSMessageTemplate;
    char* mCurrentSMessageName;
    char* mCurrentSBlockName;
    const message_template_name_map_t& mMessageTemplates;
};

#endif // LL_LLTEMPLATEMESSAGEBUILDER_H
