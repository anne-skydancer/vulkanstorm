// Frozen original owner chat command branch. Linden Research/Firestorm, LGPL-2.1.
VSOwnerChatResult reference(LLChat&chat,std::string&mesg,LLViewerObject*chatter){const auto from_id=chat.mFromID;                if ( (rlv_handler_t::isEnabled()) && (mesg.length() > 3) && (RLV_CMD_PREFIX == mesg[0]) && (CHAT_TYPE_OWNER == chat.mChatType) &&
                     ((!chatter) || (!chatter->isAttachment()) || (!chatter->isTempAttachment()) || (RlvSettings::getEnableTemporaryAttachments())) )
                {
                    mesg.erase(0, 1);
                    LLStringUtil::toLower(mesg);

                    std::string strExecuted, strFailed, strRetained, *pstr;

                    boost_tokenizer tokens(mesg, boost::char_separator<char>(",", "", boost::drop_empty_tokens));
                    for (boost_tokenizer::iterator itToken = tokens.begin(); itToken != tokens.end(); ++itToken)
                    {
                        std::string strCmd = *itToken;

                        ERlvCmdRet eRet = gRlvHandler.processCommand(from_id, strCmd, true);
                        if ( (RlvSettings::getDebug()) &&
                             ( (!RlvSettings::getDebugHideUnsetDup()) ||
                               ((RLV_RET_SUCCESS_UNSET != eRet) && (RLV_RET_SUCCESS_DUPLICATE != eRet)) ) )
                        {
                            if ( RLV_RET_SUCCESS == (eRet & RLV_RET_SUCCESS) )
                                pstr = &strExecuted;
                            else if ( RLV_RET_FAILED == (eRet & RLV_RET_FAILED) )
                                pstr = &strFailed;
                            else if (RLV_RET_RETAINED == eRet)
                                pstr = &strRetained;
                            else
                            {
                                RLV_ASSERT(false);
                                pstr = &strFailed;
                            }

                            const char* pstrSuffix = RlvStrings::getStringFromReturnCode(eRet);
                            if (pstrSuffix)
                                strCmd.append(" (").append(pstrSuffix).append(")");

                            if (!pstr->empty())
                                pstr->push_back(',');
                            pstr->append(strCmd);
                        }
                    }

                    if (RlvForceWear::instanceExists())
                        RlvForceWear::instance().done();

                    if ( (!RlvSettings::getDebug()) || ((strExecuted.empty()) && (strFailed.empty()) && (strRetained.empty())) )
                        return VSOwnerChatResult::Consumed;

                    // Silly people want comprehensive debug messages, blah :p
                    if ( (!strExecuted.empty()) && (strFailed.empty()) && (strRetained.empty()) )
                    {
                        chat.mText = " executes: @";
                        mesg = strExecuted;
                    }
                    else if ( (strExecuted.empty()) && (!strFailed.empty()) && (strRetained.empty()) )
                    {
                        chat.mText = " failed: @";
                        mesg = strFailed;
                    }
                    else if ( (strExecuted.empty()) && (strFailed.empty()) && (!strRetained.empty()) )
                    {
                        chat.mText = " retained: @";
                        mesg = strRetained;
                    }
                    else
                    {
                        chat.mText = ": @";
                        if (!strExecuted.empty())
                            mesg += "\n    - executed: @" + strExecuted;
                        if (!strFailed.empty())
                            mesg += "\n    - failed: @" + strFailed;
                        if (!strRetained.empty())
                            mesg += "\n    - retained: @" + strRetained;
                    }

                    return VSOwnerChatResult::Display;
                }return VSOwnerChatResult::NotCommand;}
