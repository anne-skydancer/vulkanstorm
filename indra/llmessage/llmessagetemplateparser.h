/**
 * @file llmessagetemplateparser.h
 * @brief Classes to parse message template.
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
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

#ifndef LL_MESSAGETEMPLATEPARSER_H
#define LL_MESSAGETEMPLATEPARSER_H

#include <string>
#include "llmessagetemplate.h"

struct VksMsgTokenizer;

// The tokenizer and parser of message_template.msg. Both are the Rust
// crate vkstorm-messages (vkstorm-rust rung three); these classes keep the
// interface the viewer and its tests use.
class LLTemplateTokenizer
{
public:
    LLTemplateTokenizer(const std::string & contents);
    LLTemplateTokenizer(const LLTemplateTokenizer& other);
    LLTemplateTokenizer& operator=(const LLTemplateTokenizer& other);
    ~LLTemplateTokenizer();

    U32 line() const;
    bool atEOF() const;
    std::string next();

    bool want(const std::string & token);
    bool wantEOF();

    VksMsgTokenizer* handle() const { return mTokenizer; }
private:
    VksMsgTokenizer* mTokenizer;
};

class LLTemplateParser
{
public:
    typedef std::list<LLMessageTemplate *>::const_iterator message_iterator;

    static LLMessageTemplate * parseMessage(LLTemplateTokenizer & tokens);
    static LLMessageBlock * parseBlock(LLTemplateTokenizer & tokens);
    static LLMessageVariable * parseVariable(LLTemplateTokenizer & tokens);

    LLTemplateParser(LLTemplateTokenizer & tokens);
    message_iterator getMessagesBegin() const;
    message_iterator getMessagesEnd() const;
    F32 getVersion() const;

private:
    F32 mVersion;
    std::list<LLMessageTemplate *> mMessages;
};

#endif
