/**
 * @file llmessagetemplateparser.cpp
 * @brief LLMessageTemplateParser implementation
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

// The template language is parsed by the Rust crate vkstorm-messages
// (vkstorm-rust rung three, see doc/rust_port.md); these classes keep their
// interface and turn the parsed descriptions into the viewer's own
// LLMessageTemplate, LLMessageBlock and LLMessageVariable objects. Where
// the parser aborted on bad input (LL_ERRS) it still does, with the same
// message.

#include "linden_common.h"
#include "llmessagetemplateparser.h"

#include "vkstorm_ffi.h"

namespace
{
    std::string token_string(const uint8_t* p, size_t n)
    {
        return std::string((const char*)p, n);
    }

    LLMessageVariable* variable_from(const VksMsgDesc* desc, uint32_t block, uint32_t var)
    {
        VksMsgVarInfo info;
        if (!vks_msg_desc_variable(desc, block, var, &info))
        {
            return NULL;
        }
        std::string name = token_string(info.name, info.name_len);
        return new LLMessageVariable(name.c_str(), (EMsgVariableType)info.type, info.size);
    }

    LLMessageBlock* block_from(const VksMsgDesc* desc, uint32_t block)
    {
        VksMsgBlockInfo info;
        if (!vks_msg_desc_block(desc, block, &info))
        {
            return NULL;
        }
        std::string name = token_string(info.name, info.name_len);
        LLMessageBlock* blockp = new LLMessageBlock(name.c_str(), (EMsgBlockType)info.type, info.number);
        for (uint32_t v = 0; v < info.var_count; ++v)
        {
            LLMessageVariable* varp = variable_from(desc, block, v);
            if (varp)
            {
                blockp->addVariable(varp->getName(), varp->getType(), varp->getSize());
                delete varp;
            }
        }
        return blockp;
    }

    LLMessageTemplate* message_from(const VksMsgDesc* desc)
    {
        VksMsgInfo info;
        if (!vks_msg_desc_message(desc, &info))
        {
            return NULL;
        }
        std::string name = token_string(info.name, info.name_len);
        LLMessageTemplate* templatep = new LLMessageTemplate(name.c_str(), info.number, (EMsgFrequency)info.frequency);
        templatep->setTrust((EMsgTrust)info.trust);
        templatep->setEncoding((EMsgEncoding)info.encoding);
        templatep->setDeprecation((EMsgDeprecation)info.deprecation);
        for (uint32_t b = 0; b < info.block_count; ++b)
        {
            LLMessageBlock* blockp = block_from(desc, b);
            if (blockp)
            {
                templatep->addBlock(blockp);
            }
        }
        return templatep;
    }
}

LLTemplateTokenizer::LLTemplateTokenizer(const std::string & contents)
    : mTokenizer(vks_msg_tokenizer_new((const uint8_t*)contents.data(), contents.size()))
{
    if (!mTokenizer)
    {
        LL_ERRS() << "Cannot tokenize the message template" << LL_ENDL;
    }
}

LLTemplateTokenizer::LLTemplateTokenizer(const LLTemplateTokenizer& other)
    : mTokenizer(vks_msg_tokenizer_clone(other.mTokenizer))
{
}

LLTemplateTokenizer& LLTemplateTokenizer::operator=(const LLTemplateTokenizer& other)
{
    if (this != &other)
    {
        vks_msg_tokenizer_free(mTokenizer);
        mTokenizer = vks_msg_tokenizer_clone(other.mTokenizer);
    }
    return *this;
}

LLTemplateTokenizer::~LLTemplateTokenizer()
{
    vks_msg_tokenizer_free(mTokenizer);
    mTokenizer = NULL;
}

U32 LLTemplateTokenizer::line() const
{
    return vks_msg_tokenizer_line(mTokenizer);
}

bool LLTemplateTokenizer::atEOF() const
{
    return vks_msg_tokenizer_at_eof(mTokenizer) != 0;
}

std::string LLTemplateTokenizer::next()
{
    const uint8_t* p = NULL;
    size_t n = 0;
    if (!vks_msg_tokenizer_next(mTokenizer, &p, &n))
    {
        LL_ERRS() << vks_msg_last_error() << LL_ENDL;
        return std::string();
    }
    return token_string(p, n);
}

bool LLTemplateTokenizer::want(const std::string & token)
{
    int32_t r = vks_msg_tokenizer_want(mTokenizer, (const uint8_t*)token.data(), token.size());
    if (r < 0)
    {
        LL_ERRS() << vks_msg_last_error() << LL_ENDL;
        return false;
    }
    return r == 1;
}

bool LLTemplateTokenizer::wantEOF()
{
    int32_t r = vks_msg_tokenizer_want_eof(mTokenizer);
    if (r < 0)
    {
        LL_ERRS() << vks_msg_last_error() << LL_ENDL;
        return false;
    }
    return r == 1;
}


// Done with tokenizer, next is the parser.

LLTemplateParser::LLTemplateParser(LLTemplateTokenizer & tokens):
    mVersion(0.f),
    mMessages()
{
    // the version number should be the first thing in the file
    if (!vks_msg_parse_version(tokens.handle(), &mVersion))
    {
        LL_ERRS() << vks_msg_last_error() << LL_ENDL;
    }
    LL_INFOS() << "### Message template version " << mVersion << "  ###" << LL_ENDL;

    while(LLMessageTemplate * templatep = parseMessage(tokens))
    {
        if (templatep->getDeprecation() != MD_DEPRECATED)
        {
            mMessages.push_back(templatep);
        }
        else
        {
            delete templatep;
        }
    }

    if(!tokens.wantEOF())
    {
        LL_ERRS() << "Expected end of template or a message, instead found: "
               << tokens.next() << " at " << tokens.line() << LL_ENDL;
    }
}

F32 LLTemplateParser::getVersion() const
{
    return mVersion;
}

LLTemplateParser::message_iterator LLTemplateParser::getMessagesBegin() const
{
    return mMessages.begin();
}

LLTemplateParser::message_iterator LLTemplateParser::getMessagesEnd() const
{
    return mMessages.end();
}


// static
LLMessageTemplate * LLTemplateParser::parseMessage(LLTemplateTokenizer & tokens)
{
    int32_t status = 0;
    VksMsgDesc* desc = vks_msg_parse_message(tokens.handle(), &status);
    if (status < 0)
    {
        LL_ERRS() << vks_msg_last_error() << LL_ENDL;
        return NULL;
    }
    if (!desc)
    {
        return NULL;
    }
    LLMessageTemplate* templatep = message_from(desc);
    vks_msg_desc_free(desc);
    return templatep;
}

// static
LLMessageBlock * LLTemplateParser::parseBlock(LLTemplateTokenizer & tokens)
{
    int32_t status = 0;
    VksMsgDesc* desc = vks_msg_parse_block(tokens.handle(), &status);
    if (status < 0)
    {
        LL_ERRS() << vks_msg_last_error() << LL_ENDL;
        return NULL;
    }
    if (!desc)
    {
        return NULL;
    }
    LLMessageBlock* blockp = block_from(desc, 0);
    vks_msg_desc_free(desc);
    return blockp;
}

// static
LLMessageVariable * LLTemplateParser::parseVariable(LLTemplateTokenizer & tokens)
{
    int32_t status = 0;
    VksMsgDesc* desc = vks_msg_parse_variable(tokens.handle(), &status);
    if (status < 0)
    {
        LL_ERRS() << vks_msg_last_error() << LL_ENDL;
        return NULL;
    }
    if (!desc)
    {
        return NULL;
    }
    LLMessageVariable* varp = variable_from(desc, 0, 0);
    vks_msg_desc_free(desc);
    return varp;
}
