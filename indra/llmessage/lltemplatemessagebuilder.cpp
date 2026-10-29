/**
 * @file lltemplatemessagebuilder.cpp
 * @brief LLTemplateMessageBuilder class implementation.
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

// The message is accumulated and packed by the Rust crate vkstorm-messages
// (vkstorm-rust rung three, see doc/rust_port.md). Blocks and variables are
// passed to it by their canonical name pointers; the typed add* methods do
// the conversions they always did (port byte order, quaternion packing)
// and hand the bytes over. Errors the builder aborted on still abort here,
// with the same text.

#include "linden_common.h"

#include "lltemplatemessagebuilder.h"

#include "llmessagetemplate.h"
#include "llmath.h"
#include "llquaternion.h"
#include "u64.h"
#include "v3dmath.h"
#include "v3math.h"
#include "v4math.h"
#include "vkstorm_ffi.h"

namespace
{
    inline uint64_t key_of(const char* name)
    {
        return (uint64_t)(uintptr_t)name;
    }
}

LLTemplateMessageBuilder::LLTemplateMessageBuilder(const message_template_name_map_t& name_template_map) :
    mBuilder(vks_msg_builder_new()),
    mCurrentSMessageData(NULL),
    mCurrentSMessageTemplate(NULL),
    mCurrentSMessageName(NULL),
    mCurrentSBlockName(NULL),
    mMessageTemplates(name_template_map)
{
}

//virtual
LLTemplateMessageBuilder::~LLTemplateMessageBuilder()
{
    delete mCurrentSMessageData;
    mCurrentSMessageData = NULL;
    vks_msg_builder_free(mBuilder);
    mBuilder = NULL;
}

// virtual
void LLTemplateMessageBuilder::newMessage(const char *name)
{
    delete mCurrentSMessageData;
    mCurrentSMessageData = NULL;

    char* namep = (char*)name;
    message_template_name_map_t::const_iterator it = mMessageTemplates.find(name);
    if (it != mMessageTemplates.end())
    {
        mCurrentSMessageTemplate = it->second;
        mCurrentSMessageName = namep;
        mCurrentSBlockName = NULL;
        if (vks_msg_builder_new_message(mBuilder, mCurrentSMessageTemplate->compiled()))
        {
            LL_WARNS() << "Sending deprecated message " << namep << LL_ENDL;
        }
    }
    else
    {
        LL_ERRS() << "newMessage - Message " << name << " not registered" << LL_ENDL;
    }
}

// virtual
void LLTemplateMessageBuilder::clearMessage()
{
    vks_msg_builder_clear(mBuilder);

    mCurrentSMessageTemplate = NULL;

    delete mCurrentSMessageData;
    mCurrentSMessageData = NULL;

    mCurrentSMessageName = NULL;
    mCurrentSBlockName = NULL;
}

// virtual
void LLTemplateMessageBuilder::nextBlock(const char* blockname)
{
    char *bnamep = (char *)blockname;

    if (!mCurrentSMessageTemplate)
    {
        LL_ERRS() << "newMessage not called prior to setBlock" << LL_ENDL;
        return;
    }

    int32_t rc = vks_msg_builder_next_block(mBuilder, key_of(bnamep));
    switch (rc)
    {
    case VKS_MSG_OK:
        mCurrentSBlockName = bnamep;
        return;
    case VKS_MSG_ERR_NOT_A_BLOCK:
        LL_ERRS() << "LLTemplateMessageBuilder::nextBlock " << bnamep
            << " not a block in " << mCurrentSMessageTemplate->mName << LL_ENDL;
        return;
    case VKS_MSG_ERR_SINGLE_REPEATED:
        LL_ERRS() << "LLTemplateMessageBuilder::nextBlock called multiple times"
            << " for " << bnamep << " but is type MBT_SINGLE" << LL_ENDL;
        return;
    case VKS_MSG_ERR_MULTIPLE_EXCEEDED:
        LL_ERRS() << "LLTemplateMessageBuilder::nextBlock " << vks_msg_last_error()
            << " for " << bnamep << LL_ENDL;
        return;
    case VKS_MSG_ERR_TOO_MANY_BLOCKS:
        LL_ERRS() << "Trying to pack too many blocks into MBT_VARIABLE type "
               << "(limited to " << MAX_BLOCKS << ")" << LL_ENDL;
        return;
    default:
        LL_ERRS() << "LLTemplateMessageBuilder::nextBlock " << bnamep << ": " << vks_msg_last_error() << LL_ENDL;
        return;
    }
}

// TODO: Remove this horror...
bool LLTemplateMessageBuilder::removeLastBlock()
{
    if (mCurrentSBlockName && mCurrentSMessageTemplate)
    {
        if (vks_msg_builder_remove_last_block(mBuilder))
        {
            return true;
        }
        LL_WARNS() << "not blowing away the only block of message "
                << mCurrentSMessageName
                << ". Block: " << mCurrentSBlockName
                << LL_ENDL;
    }
    return false;
}

// add data to variable in current block
void LLTemplateMessageBuilder::addData(const char *varname, const void *data, EMsgVariableType type, S32 size)
{
    char *vnamep = (char *)varname;

    // do we have a current message?
    if (!mCurrentSMessageTemplate)
    {
        LL_ERRS() << "newMessage not called prior to addData" << LL_ENDL;
        return;
    }

    // do we have a current block?
    if (!mCurrentSBlockName)
    {
        LL_ERRS() << "setBlock not called prior to addData" << LL_ENDL;
        return;
    }

    int32_t rc = vks_msg_builder_add(mBuilder, key_of(vnamep), (const uint8_t*)data, size > 0 ? (size_t)size : 0, type);
    if (rc >= 0)
    {
        if (rc & 1)
        {
            LL_WARNS() << "Field " << varname << " is a Variable 1 but program "
                   << "attempted to stuff more than 255 bytes in "
                   << "(" << size << ").  Clamping size and truncating data." << LL_ENDL;
        }
        if (rc & 2)
        {
            LL_WARNS() << "Type mismatch in LLMsgVarData::addData for " << vnamep << LL_ENDL;
        }
        return;
    }
    switch (rc)
    {
    case VKS_MSG_ERR_NO_MESSAGE:
        LL_ERRS() << "newMessage not called prior to addData" << LL_ENDL;
        return;
    case VKS_MSG_ERR_NO_BLOCK:
        LL_ERRS() << "setBlock not called prior to addData" << LL_ENDL;
        return;
    case VKS_MSG_ERR_NOT_A_VARIABLE:
        LL_ERRS() << vnamep << " not a variable in block " << mCurrentSBlockName << " of " << mCurrentSMessageTemplate->mName << LL_ENDL;
        return;
    case VKS_MSG_ERR_SIZE_MISMATCH:
        LL_ERRS() << varname << " " << vks_msg_last_error() << LL_ENDL;
        return;
    default:
        LL_ERRS() << varname << ": " << vks_msg_last_error() << LL_ENDL;
        return;
    }
}

void LLTemplateMessageBuilder::addBinaryData(const char *varname,
                                            const void *data, S32 size)
{
    addData(varname, data, MVT_FIXED, size);
}

void LLTemplateMessageBuilder::addS8(const char *varname, S8 s)
{
    addData(varname, &s, MVT_S8, sizeof(s));
}

void LLTemplateMessageBuilder::addU8(const char *varname, U8 u)
{
    addData(varname, &u, MVT_U8, sizeof(u));
}

void LLTemplateMessageBuilder::addS16(const char *varname, S16 i)
{
    addData(varname, &i, MVT_S16, sizeof(i));
}

void LLTemplateMessageBuilder::addU16(const char *varname, U16 i)
{
    addData(varname, &i, MVT_U16, sizeof(i));
}

void LLTemplateMessageBuilder::addF32(const char *varname, F32 f)
{
    addData(varname, &f, MVT_F32, sizeof(f));
}

void LLTemplateMessageBuilder::addS32(const char *varname, S32 s)
{
    addData(varname, &s, MVT_S32, sizeof(s));
}

void LLTemplateMessageBuilder::addU32(const char *varname, U32 u)
{
    addData(varname, &u, MVT_U32, sizeof(u));
}

void LLTemplateMessageBuilder::addU64(const char *varname, U64 lu)
{
    addData(varname, &lu, MVT_U64, sizeof(lu));
}

void LLTemplateMessageBuilder::addF64(const char *varname, F64 d)
{
    addData(varname, &d, MVT_F64, sizeof(d));
}

void LLTemplateMessageBuilder::addIPAddr(const char *varname, U32 u)
{
    addData(varname, &u, MVT_IP_ADDR, sizeof(u));
}

void LLTemplateMessageBuilder::addIPPort(const char *varname, U16 u)
{
    u = htons(u);
    addData(varname, &u, MVT_IP_PORT, sizeof(u));
}

void LLTemplateMessageBuilder::addBOOL(const char* varname, bool b)
{
    U8 temp = (b != 0);
    addData(varname, &temp, MVT_BOOL, sizeof(temp));
}

void LLTemplateMessageBuilder::addString(const char* varname, const char* s)
{
    if (s)
        addData( varname, (void *)s, MVT_VARIABLE, (S32)strlen(s) + 1);  /* Flawfinder: ignore */
    else
        addData( varname, NULL, MVT_VARIABLE, 0);
}

void LLTemplateMessageBuilder::addString(const char* varname, const std::string& s)
{
    if (s.size())
        addData( varname, (void *)s.c_str(), MVT_VARIABLE, (S32)(s.size()) + 1);
    else
        addData( varname, NULL, MVT_VARIABLE, 0);
}

void LLTemplateMessageBuilder::addVector3(const char *varname, const LLVector3& vec)
{
    addData(varname, vec.mV, MVT_LLVector3, sizeof(vec.mV));
}

void LLTemplateMessageBuilder::addVector4(const char *varname, const LLVector4& vec)
{
    addData(varname, vec.mV, MVT_LLVector4, sizeof(vec.mV));
}

void LLTemplateMessageBuilder::addVector3d(const char *varname, const LLVector3d& vec)
{
    addData(varname, vec.mdV, MVT_LLVector3d, sizeof(vec.mdV));
}

void LLTemplateMessageBuilder::addQuat(const char *varname, const LLQuaternion& quat)
{
    addData(varname, quat.packToVector3().mV, MVT_LLQuaternion, sizeof(LLVector3));
}

void LLTemplateMessageBuilder::addUUID(const char *varname, const LLUUID& uuid)
{
    addData(varname, uuid.mData, MVT_LLUUID, sizeof(uuid.mData));
}

void LLTemplateMessageBuilder::compressMessage(U8*& buf_ptr, U32& buffer_length)
{
    if(ME_ZEROCODED == mCurrentSMessageTemplate->getEncoding())
    {
        // Encoded send buffer needs to be slightly larger since the zero
        // coding can potentially increase the size of the send data.
        static U8 encodedSendBuffer[2 * MAX_BUFFER_SIZE];
        int32_t coded = vks_msg_zero_code(buf_ptr, buffer_length, encodedSendBuffer, sizeof(encodedSendBuffer));
        if (coded > 0)
        {
            buf_ptr = encodedSendBuffer;
            buffer_length = (U32)coded;
        }
    }
}

bool LLTemplateMessageBuilder::isMessageFull(const char* blockname) const
{
    int32_t rc = vks_msg_builder_is_full(mBuilder, key_of(blockname), blockname != NULL);
    if (rc < 0)
    {
        LL_ERRS() << "LLTemplateMessageBuilder::isMessageFull " << (blockname ? blockname : "")
            << ": " << vks_msg_last_error() << LL_ENDL;
        return false;
    }
    return rc == 1;
}

// make sure that all the desired data is in place and then copy the data into MAX_BUFFER_SIZEd buffer
U32 LLTemplateMessageBuilder::buildMessage(
    U8* buffer,
    U32 buffer_size,
    U8 offset_to_data)
{
    // do we have a current message?
    if (!mCurrentSMessageTemplate)
    {
        LL_ERRS() << "newMessage not called prior to buildMessage" << LL_ENDL;
        return 0;
    }

    int64_t result = vks_msg_builder_build(mBuilder, buffer, buffer_size, offset_to_data);
    if (result < 0)
    {
        LL_ERRS() << vks_msg_last_error() << LL_ENDL;
        return 0;
    }
    return (U32)result;
}

void LLTemplateMessageBuilder::copyFromMessageData(const LLMsgData& data)
{
    // copy the blocks
    // counting variables used to encode multiple block info
    S32 block_count = 0;
    char *block_name = NULL;

    // loop through msg blocks to loop through variables, totalling up size
    // data and filling the new (send) message
    LLMsgData::msg_blk_data_map_t::const_iterator iter =
        data.mMemberBlocks.begin();
    LLMsgData::msg_blk_data_map_t::const_iterator end =
        data.mMemberBlocks.end();
    for(; iter != end; ++iter)
    {
        const LLMsgBlkData* mbci = iter->second;
        if(!mbci) continue;

        // do we need to encode a block code?
        if (block_count == 0)
        {
            block_count = mbci->mBlockNumber;
            block_name = (char *)mbci->mName;
        }

        // counting down mutliple blocks
        block_count--;

        nextBlock(block_name);

        // now loop through the variables
        LLMsgBlkData::msg_var_data_map_t::const_iterator dit = mbci->mMemberVarData.begin();
        LLMsgBlkData::msg_var_data_map_t::const_iterator dend = mbci->mMemberVarData.end();

        for(; dit != dend; ++dit)
        {
            const LLMsgVarData& mvci = *dit;
            addData(mvci.getName(), mvci.getData(), mvci.getType(), mvci.getSize());
        }
    }
}

//virtual
void LLTemplateMessageBuilder::copyFromLLSD(const LLSD&)
{
    // TODO
}

//virtual
void LLTemplateMessageBuilder::setBuilt(bool b) { vks_msg_builder_set_built(mBuilder, b); }

//virtual
bool LLTemplateMessageBuilder::isBuilt() const {return vks_msg_builder_is_built(mBuilder) != 0;}

//virtual
bool LLTemplateMessageBuilder::isClear() const {return vks_msg_builder_is_clear(mBuilder) != 0;}

//virtual
S32 LLTemplateMessageBuilder::getMessageSize() {return vks_msg_builder_message_size(mBuilder);}

//virtual
const char* LLTemplateMessageBuilder::getMessageName() const
{
    return mCurrentSMessageName;
}

LLMsgData* LLTemplateMessageBuilder::getCurrentMessage() const
{
    delete mCurrentSMessageData;
    mCurrentSMessageData = NULL;
    if (!mCurrentSMessageTemplate)
    {
        return NULL;
    }
    mCurrentSMessageData = new LLMsgData(mCurrentSMessageTemplate->mName);
    U32 bi = 0;
    for (LLMessageTemplate::message_block_map_t::const_iterator bit = mCurrentSMessageTemplate->mMemberBlocks.begin();
         bit != mCurrentSMessageTemplate->mMemberBlocks.end(); ++bit, ++bi)
    {
        const LLMessageBlock* block = *bit;
        S32 repeats = vks_msg_builder_block_repeats(mBuilder, bi);
        for (S32 r = 0; r < repeats; ++r)
        {
            LLMsgBlkData* blockp = new LLMsgBlkData(block->mName, r == 0 ? repeats : r);
            blockp->mName = block->mName + r;
            mCurrentSMessageData->addBlock(blockp);
            U32 vi = 0;
            for (LLMessageBlock::message_variable_map_t::const_iterator vit = block->mMemberVariables.begin();
                 vit != block->mMemberVariables.end(); ++vit, ++vi)
            {
                const LLMessageVariable* var = *vit;
                blockp->addVariable(var->getName(), var->getType());
                size_t len = 0;
                int32_t data_size = -1;
                const uint8_t* d = vks_msg_builder_slot(mBuilder, bi, (uint32_t)r, vi, &len, &data_size);
                if (d)
                {
                    blockp->addData(var->getName(), d, (S32)len, var->getType(), data_size);
                }
            }
        }
    }
    return mCurrentSMessageData;
}
