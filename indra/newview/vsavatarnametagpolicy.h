/**
 * @File llvoavatar.cpp
 * @brief Implementation of LLVOAvatar class which is a derivation of LLViewerObject
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
// Shared avatar name/bubble policy, consumed by real GL/native avatar owners.
// LGPL-2.1. Preserve the original preference, status, privacy and name rules.
#pragma once
#include <type_traits>
struct VSAvatarNameTagPolicy
{
    enum {RENDER_NAME_NEVER,RENDER_NAME_ALWAYS,RENDER_NAME_FADE};
    static constexpr S32 MAX_BUBBLE_CHAT_UTTERANCES=12;
    static constexpr F32 CHAT_FADE_TIME=8.f,BUBBLE_CHAT_TIME=CHAT_FADE_TIME*3.f;
    static constexpr F32 NAMETAG_UPDATE_THRESHOLD=.3f,NAMETAG_VERTICAL_SCREEN_OFFSET=25.f,NAMETAG_VERT_OFFSET_WEIGHT=.17f;
    template<class Owner>static void bind(Owner& avatar)
    {
        if constexpr(std::is_same_v<Owner,LLVOAvatar>)avatar.mNameText->setSourceObject(&avatar);
        else avatar.bindNameTag();
    }
    template<class Owner>static bool canShow(Owner& avatar)
    {
        if constexpr(std::is_same_v<Owner,LLVOAvatar>)return RlvActions::canShowNameTag(&avatar);
        else return avatar.canShowNameTag();
    }
    template<class Owner>static LLVector3 agentPosition(Owner& avatar,const LLVector3& position)
    {
        if constexpr(std::is_same_v<Owner,LLVOAvatar>)return position;
        else return position+avatar.mAgentOffset;
    }
    template<class Owner>static F32 waterHeight(Owner& avatar)
    {
        if constexpr(std::is_same_v<Owner,LLVOAvatar>)return avatar.getRegion()->getWaterHeight();
        else return avatar.mWaterHeight;
    }
    template<class Owner>static void update(Owner& avatar,const LLVector3& root_pos_last)
    {
        auto& mAppAngle=avatar.mAppAngle;
        auto& mChatTimer=avatar.mChatTimer;
        auto& mChats=avatar.mChats;
        auto& mNameText=avatar.mNameText;
        auto& mRenderGroupTitles=avatar.mRenderGroupTitles;
        auto& mTimeVisible=avatar.mTimeVisible;
        auto& mTyping=avatar.mTyping;
        auto& mVisibleChat=avatar.mVisibleChat;
        auto& mVisibleTyping=avatar.mVisibleTyping;

    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;

    // update chat bubble
    //--------------------------------------------------------------------
    // draw text label over character's head
    //--------------------------------------------------------------------
    if (mChatTimer.getElapsedTimeF32() > BUBBLE_CHAT_TIME)
    {
        mChats.clear();
    }

    const F32 time_visible = mTimeVisible.getElapsedTimeF32();
    static LLCachedControl<F32> NAME_SHOW_TIME(gSavedSettings, "RenderNameShowTime"); // seconds
    static LLCachedControl<F32> FADE_DURATION(gSavedSettings, "RenderNameFadeDuration"); // seconds
    static LLCachedControl<bool> use_chat_bubbles(gSavedSettings, "UseChatBubbles");
    static LLCachedControl<bool> use_typing_bubbles(gSavedSettings, "UseTypingBubbles");

// [RLVa:KB] - Checked: RLVa-2.0.1
    bool fRlvShowAvTag = true, fRlvShowAvName = true;
    if (RlvActions::isRlvEnabled())
    {
        fRlvShowAvTag = canShow(avatar);
        fRlvShowAvName = (fRlvShowAvTag) && (RlvActions::canShowName(RlvActions::SNC_DEFAULT, avatar.getID()));
    }
// [/RLVa:KB]
    bool visible_chat = use_chat_bubbles && (mChats.size() || mTyping);
    bool visible_typing = use_typing_bubbles && mTyping;
    bool render_name =  visible_chat ||
                visible_typing ||
// [RLVa:KB] - Checked: RLVa-2.0.1
                        ((fRlvShowAvTag) &&
// [/RLVa:KB]
                        ((LLVOAvatar::sRenderName == RENDER_NAME_ALWAYS) ||
                         (LLVOAvatar::sRenderName == RENDER_NAME_FADE && time_visible < NAME_SHOW_TIME)));
    // If it's your own avatar, don't draw in mouselook, and don't
    // draw if we're specifically hiding our own name.
    if (avatar.isSelf())
    {
        static LLCachedControl<bool> render_name_show_self(gSavedSettings, "RenderNameShowSelf");
        static LLCachedControl<S32> name_tag_mode(gSavedSettings, "AvatarNameTagMode");
        render_name = render_name
            && !gAgentCamera.cameraMouselook()
            && (visible_chat || (render_name_show_self && name_tag_mode));
    }

    if ( !render_name )
    {
        if (mNameText)
        {
            // ...clean up old name tag
            mNameText->markDead();
            mNameText = NULL;
            LLVOAvatar::sNumVisibleChatBubbles--;
        }
        return;
    }

    bool new_name = false;
    if (visible_chat != mVisibleChat)
    {
        mVisibleChat = visible_chat;
        new_name = true;
    }
        if (visible_typing != mVisibleTyping)
        {
            mVisibleTyping = visible_typing;
            new_name = true;
        }

// [RLVa:KB] - Checked: RLVa-0.2.0
    if (!fRlvShowAvName)
    {
        if (mRenderGroupTitles)
        {
            mRenderGroupTitles = false;
            new_name = true;
        }
    }
    else if (LLVOAvatar::sRenderGroupTitles != mRenderGroupTitles)
// [/RLVa]
//  if (LLVOAvatar::sRenderGroupTitles != mRenderGroupTitles)
    {
        mRenderGroupTitles = LLVOAvatar::sRenderGroupTitles;
        new_name = true;
    }

    // First Calculate Alpha
    // If alpha > 0, create mNameText if necessary, otherwise delete it
    F32 alpha = 0.f;
    if (mAppAngle > 5.f)
    {
        const F32 START_FADE_TIME = NAME_SHOW_TIME - FADE_DURATION;
        if (!visible_chat && !visible_typing && LLVOAvatar::sRenderName == RENDER_NAME_FADE && time_visible > START_FADE_TIME)
        {
            alpha = 1.f - (time_visible - START_FADE_TIME) / FADE_DURATION;
        }
        else
        {
            // ...not fading, full alpha
            alpha = 1.f;
        }
    }
    else if (mAppAngle > 2.f)
    {
        // far away is faded out also
        alpha = (mAppAngle-2.f)/3.f;
    }

    if (alpha <= 0.f)
    {
        if (mNameText)
        {
            mNameText->markDead();
            mNameText = NULL;
            LLVOAvatar::sNumVisibleChatBubbles--;
        }
        return;
    }

    if (!mNameText)
    {
        mNameText = static_cast<LLHUDNameTag*>( LLHUDObject::addHUDObject(
            LLHUDObject::LL_HUD_NAME_TAG) );
        //mNameText->setMass(10.f);
        bind(avatar);
        mNameText->setVertAlignment(LLHUDNameTag::ALIGN_VERT_TOP);
        mNameText->setVisibleOffScreen(true);
        mNameText->setMaxLines(11);
        mNameText->setFadeDistance(CHAT_NORMAL_RADIUS, 5.f);
        LLVOAvatar::sNumVisibleChatBubbles++;
        new_name = true;
    }

    mNameText->setPositionAgent(root_pos_last);

    avatar.idleUpdateNameTagText(new_name);
    // Wolfspirit: Following thing is already handled in LLHUDNameTag::lineSegmentIntersect
    // Fixing bubblechat alpha flashing with commenting this out.
    // idleUpdateNameTagAlpha(new_name, alpha);

    }
    template<class Owner>static void text(Owner& avatar,bool new_name)
    {
        auto& mAttachmentSurfaceArea=avatar.mAttachmentSurfaceArea;
        auto& mChats=avatar.mChats;
        auto& mClientTagData=avatar.mClientTagData;
        auto& mDistanceString=avatar.mDistanceString;
        auto& mLastRezzedStatus=avatar.mLastRezzedStatus;
        auto& mNameAppearance=avatar.mNameAppearance;
        auto& mNameArc=avatar.mNameArc;
        auto& mNameArcColor=avatar.mNameArcColor;
        auto& mNameAutoResponse=avatar.mNameAutoResponse;
        auto& mNameAway=avatar.mNameAway;
        auto& mNameCloud=avatar.mNameCloud;
        auto& mNameColor=avatar.mNameColor;
        auto& mNameDoNotDisturb=avatar.mNameDoNotDisturb;
        auto& mNameFirstname=avatar.mNameFirstname;
        auto& mNameFriend=avatar.mNameFriend;
        auto& mNameIsSet=avatar.mNameIsSet;
        auto& mNameIsTyping=avatar.mNameIsTyping;
        auto& mNameLastname=avatar.mNameLastname;
        auto& mNameMute=avatar.mNameMute;
        auto& mNameText=avatar.mNameText;
        auto& mSignaledAnimations=avatar.mSignaledAnimations;
        auto& mTitle=avatar.mTitle;
        auto& mTyping=avatar.mTyping;
        auto& mTypingTimer=avatar.mTypingTimer;
        auto& mVisibleChat=avatar.mVisibleChat;
        auto& mVisibleTyping=avatar.mVisibleTyping;
        auto& mVisualComplexity=avatar.mVisualComplexity;

    LLNameValue *title = avatar.getNVPair("Title");
    LLNameValue* firstname = avatar.getNVPair("FirstName");
    LLNameValue* lastname = avatar.getNVPair("LastName");

    // Avatars must have a first and last name
    if (!firstname || !lastname) return;

    // <FS:Ansariel> OpenSim chat distance compatibility
    static const F32 chat_range_whisper_squared = (F32)(LFSimFeatureHandler::getInstance()->whisperRange() * LFSimFeatureHandler::getInstance()->whisperRange());
    static const F32 chat_range_say_squared = (F32)(LFSimFeatureHandler::getInstance()->sayRange() * LFSimFeatureHandler::getInstance()->sayRange());
    static const F32 chat_range_shout_squared = (F32)(LFSimFeatureHandler::getInstance()->shoutRange() * LFSimFeatureHandler::getInstance()->shoutRange());
    // </FS:Ansariel>

// [RLVa:KB] - Checked: RLVa-2.0.1
    bool fRlvShowAvName = RlvActions::canShowName(RlvActions::SNC_DEFAULT, avatar.getID());
// [/RLVa:KB]
    // <FS:Ansariel> Show auto-response in nametag
    static LLCachedControl<bool> fsAutorespondMode(gSavedPerAccountSettings, "FSAutorespondMode");
    static LLCachedControl<bool> fsAutorespondNonFriendsMode(gSavedPerAccountSettings, "FSAutorespondNonFriendsMode");
    static LLCachedControl<bool> fsShowAutorespondInNametag(gSavedSettings, "FSShowAutorespondInNametag");
    bool is_autoresponse = avatar.isSelf() && fsShowAutorespondInNametag && (fsAutorespondMode || fsAutorespondNonFriendsMode);
    // </FS:Ansariel>
    // <FS:Ansariel> FIRE-3475: Show typing in nametag
    static LLCachedControl<bool> fsShowTypingStateInNameTag(gSavedSettings, "FSShowTypingStateInNameTag");
    bool is_typing = !avatar.isSelf() && mTyping && fsShowTypingStateInNameTag;
    // </FS:Ansariel>
    bool is_away = mSignaledAnimations.find(ANIM_AGENT_AWAY)  != mSignaledAnimations.end();
    bool is_do_not_disturb = mSignaledAnimations.find(ANIM_AGENT_DO_NOT_DISTURB) != mSignaledAnimations.end();
    bool is_appearance = mSignaledAnimations.find(ANIM_AGENT_CUSTOMIZE) != mSignaledAnimations.end();
    bool is_muted;
    if (avatar.isSelf())
    {
        is_muted = false;
    }
    else
    {
        is_muted = avatar.isInMuteList();
    }
//  bool is_friend = avatar.isBuddy();
// [RLVa:KB] - Checked: RLVa-1.2.2
    bool is_friend = fRlvShowAvName && avatar.isBuddy();
// [/RLVa:KB]
    bool is_cloud = avatar.getHasMissingParts();

    if (is_appearance != mNameAppearance)
    {
        if (is_appearance)
        {
            avatar.debugAvatarRezTime("AvatarRezEnteredAppearanceNotification","entered appearance mode");
        }
        else
        {
            avatar.debugAvatarRezTime("AvatarRezLeftAppearanceNotification","left appearance mode");
        }
    }
    // <FS:CR> Colorize name tags
    //LLColor4 name_tag_color = avatar.getNameTagColor(is_friend);
    LLColor4 name_tag_color = avatar.getNameTagColor();
    // </FS:CR>
    LLColor4 distance_color = name_tag_color;
    std::string distance_string;

    // Wolfspirit: If we don't need to display a friend,
    // if we aren't self, if we use colored Clienttags and if we have a color
    // then use that color as name_tag_color
    static LLUICachedControl<bool> show_friends("NameTagShowFriends");
    static LLUICachedControl<U32> color_client_tags("FSColorClienttags");
    bool special_color_override = (show_friends && (is_friend || LGGContactSets::getInstance()->hasFriendColorThatShouldShow(avatar.getID(), ContactSetType::TAG))) ||
                                    LLNetMap::hasAvatarMarkColor(avatar.getID());
    if (mClientTagData.has("color")
        && !special_color_override
        && color_client_tags && !avatar.isSelf())
    {
        name_tag_color = mClientTagData["color"];
    }

    // <FS:Ansariel> Color name tags based on distance
    static LLCachedControl<bool> show_distance_color_tag(gSavedSettings, "FSTagShowDistanceColors");
    static LLCachedControl<bool> show_distance_in_tag(gSavedSettings, "FSTagShowDistance");
    // <FS:CR> FIRE-6664: Add whisper range to color tags
    static LLUIColor tag_whisper_color = LLUIColorTable::instance().getColor("NameTagWhisperDistanceColor", LLColor4::green);
    // </FS:CR> FIRE-6664: Add whisper range to color tags
    static LLUIColor tag_chat_color = LLUIColorTable::instance().getColor("NameTagChatDistanceColor", LLColor4::green);
    static LLUIColor tag_shout_color = LLUIColorTable::instance().getColor("NameTagShoutDistanceColor", LLColor4::yellow);
    static LLUIColor tag_beyond_shout_color = LLUIColorTable::instance().getColor("NameTagBeyondShoutDistanceColor", LLColor4::red);

    if (!avatar.isSelf() && (show_distance_color_tag || show_distance_in_tag))
    {
        F64 distance_squared = dist_vec_squared(avatar.getPositionGlobal(), gAgent.getPositionGlobal());
        // <FS:CR> FIRE-6664: Add whisper range color tag
        if (distance_squared <= chat_range_whisper_squared)
        {
            distance_color = tag_whisper_color;
        }
        else if (distance_squared <= chat_range_say_squared)
        // </FS:CR> FIRE-6664: Add whisper range color tag
        {
            distance_color = tag_chat_color;
        }
        else if (distance_squared <= chat_range_shout_squared)
        {
            distance_color = tag_shout_color;
        }
        else
        {
            distance_color = tag_beyond_shout_color;
        }

        if (show_distance_in_tag)
        {
            distance_string = llformat("%.02f m", sqrt(distance_squared));
        }

        // Override nametag color only if friend color is disabled
        // or avatar is not a friend nor has a contact set color
        if (show_distance_color_tag && !special_color_override)
        {
            name_tag_color = distance_color;
        }
    }
    // </FS:Ansariel>

    // <FS:Ansariel> Show ARW in nametag options (for Jelly Dolls)
    static LLCachedControl<bool> show_arw_tag(gSavedSettings, "FSTagShowARW");
    static LLCachedControl<bool> show_too_complex_only_arw_tag(gSavedSettings, "FSTagShowTooComplexOnlyARW");
    static LLCachedControl<bool> show_own_arw_tag(gSavedSettings, "FSTagShowOwnARW");
    U32 complexity(0);
    LLColor4 complexity_color(LLColor4::grey1); // default if we're not limiting the complexity

    if (show_arw_tag &&
       ((avatar.isSelf() && show_own_arw_tag) ||
       (!avatar.isSelf() && (!show_too_complex_only_arw_tag || avatar.isTooComplex()))))
    {
        complexity = mVisualComplexity;

        // Show complexity color if we're limiting and not showing our own ARW...
        static LLCachedControl<U32> max_render_cost(gSavedSettings, "RenderAvatarMaxComplexity", 0);
        if (max_render_cost != 0 && !avatar.isSelf())
        {
            // This calculation is copied from idleUpdateRenderComplexity()
            F32 green_level = 1.f - llclamp(((F32)complexity - (F32)max_render_cost) / (F32)max_render_cost, 0.f, 1.f);
            F32 red_level = llmin((F32)complexity / (F32)max_render_cost, 1.f);
            complexity_color.set(red_level, green_level, 0.f, 1.f);
        }
    }
    // </FS:Ansariel>

    // Rebuild name tag if state change detected
    if (!mNameIsSet
        || new_name
        // <FS:Ansariel> FIRE-13414: Avatar name isn't updated when the simulator sends a new name
        || (!LLGridManager::instance().isInSecondLife() && (firstname->getString() != mNameFirstname || lastname->getString() != mNameLastname))
        // </FS:Ansariel>
        || (!title && !mTitle.empty())
        || (title && mTitle != title->getString())
        || is_away != mNameAway
        || is_do_not_disturb != mNameDoNotDisturb
        || is_autoresponse != mNameAutoResponse
        || is_muted != mNameMute
        || is_appearance != mNameAppearance
        || is_friend != mNameFriend
        || is_cloud != mNameCloud
        || name_tag_color != mNameColor
        || is_typing != mNameIsTyping
        || distance_string != mDistanceString
        // <FS:Ansariel> Show Arc in nametag (for Jelly Dolls)
        || complexity != mNameArc
        || complexity_color != mNameArcColor)
    {

        //WS: If we got a uuid and if we know if it's id_based or not, ask FSDATA for the other tagdata, before we display it.
        if (mClientTagData.has("uuid") && mClientTagData.has("id_based"))
        {
            LLColor4 color;
            if (mClientTagData.has("tex_color"))
            {
                color.setValue(mClientTagData["tex_color"]);
            }
            else
            {
                color = LLColor4::black;
            }
            mClientTagData = FSData::getInstance()->resolveClientTag(LLUUID(mClientTagData["uuid"].asString()),
                                                                     mClientTagData["id_based"].asBoolean(),
                                                                     color);
        }

        avatar.clearNameTag();

        // <FS:Ansariel> Show auto-response in nametag
        //if (is_away || is_muted || is_do_not_disturb || is_appearance)
        if (is_away || is_muted || is_do_not_disturb || is_autoresponse || is_appearance || is_typing)
        // </FS:Ansariel>
        {
            std::string line;
            if (is_away)
            {
                line += LLTrans::getString("AvatarAway");
                line += ", ";
            }
            if (is_do_not_disturb)
            {
                line += LLTrans::getString("AvatarDoNotDisturb");
                line += ", ";
            }
            // <FS:Ansariel> Show auto-response in nametag
            if (is_autoresponse)
            {
                line += LLTrans::getString("AvatarAutoResponse");
                line += ", ";
            }
            // </FS:Ansariel>
            if (is_muted)
            {
                line += LLTrans::getString("AvatarMuted");
                line += ", ";
            }
            if (is_appearance)
            {
                line += LLTrans::getString("AvatarEditingAppearance");
                line += ", ";
            }
            if (is_cloud && !is_muted)
            {
                line += LLTrans::getString("LoadingData");
                line += ", ";
            }
            // <FS:Ansariel> FIRE-3475: Show typing in nametag
            if (is_typing)
            {
                line += LLTrans::getString("AvatarTyping");
                line += ", ";
            }
            // </FS:Ansariel>
            // trim last ", "
            line.resize( line.length() - 2 );
            avatar.addNameTagLine(line, name_tag_color, LLFontGL::NORMAL,
                LLFontGL::getFontSansSerifSmall());
        }

        // RENDER_GROUP_TITLE_SELF / _ALWAYS of the enum in llvoavatar.cpp.
        constexpr S32 render_group_title_self = 1, render_group_title_always = 2;
        bool render_title = (LLVOAvatar::sRenderGroupTitles == render_group_title_always) ||
                            (avatar.isSelf() && (LLVOAvatar::sRenderGroupTitles == render_group_title_self));

//      if (render_title && title && title->getString() && title->getString()[0] != '\0')
// [RLVa:KB] - Checked: RLVa-1.2.2
        if (render_title && fRlvShowAvName && title && title->getString() && title->getString()[0] != '\0')
// [/RLVa:KB]
        {
            std::string title_str = title->getString();
            LLStringFn::replace_ascii_controlchars(title_str,LL_UNKNOWN_CHAR);
            avatar.addNameTagLine(title_str, name_tag_color, LLFontGL::NORMAL,
                LLFontGL::getFontSansSerifSmall(), true);
        }

        static LLUICachedControl<bool> show_display_names("NameTagShowDisplayNames", true);
        static LLUICachedControl<bool> show_usernames("NameTagShowUsernames", true);
        static LLUICachedControl<bool> show_rez_status("NameTagDebugAVRezState", false);
        static LLUICachedControl<bool> colorize_username("FSColorUsername");    // <FS:CR> FIRE-1061
        static LLUICachedControl<bool> show_legacynames("FSNameTagShowLegacyUsernames");

        if (LLAvatarName::useDisplayNames())
        {
            LLAvatarName av_name;
            if (!LLAvatarNameCache::get(avatar.getID(), &av_name))
            {
                // Force a rebuild at next idle
                // Note: do not connect a callback on idle().
                avatar.clearNameTag();
            }

// [RLVa:KB] - Checked: RLVa-1.2.2
            if ( (fRlvShowAvName) || (avatar.isSelf()) )
            {
// [/RLVa:KB]
                // Might be blank if name not available yet, that's OK
                if (show_display_names)
                {

                    if (mClientTagData.has("name") && !mClientTagData["name"].asString().empty())
                    {
                        avatar.addNameTagLine((av_name.isDisplayNameDefault() ? av_name.getUserNameForDisplay() : av_name.getDisplayName()) +" (" + mClientTagData["name"].asString() + ")",name_tag_color,LLFontGL::NORMAL, LLFontGL::getFontSansSerif(), true, (!av_name.getDisplayName().empty()) );
                    }
                    else
                    {
                        avatar.addNameTagLine((av_name.isDisplayNameDefault() ? av_name.getUserNameForDisplay() : av_name.getDisplayName()), name_tag_color, LLFontGL::NORMAL, LLFontGL::getFontSansSerif(), true, true);
                    }
                }
                // Suppress SLID display if display name matches exactly (ugh)
                if (show_usernames && !av_name.isDisplayNameDefault())
                {
                    // *HACK: Desaturate the color
                    // <FS:CR> FIRE-1061
                    LLColor4 username_color;
                    if (colorize_username)
                    {
                        username_color = LLUIColorTable::instance().getColor("NameTagUsername", LLColor4::white);
                    }
                    else
                    {
                        username_color = name_tag_color * 0.83f;
                    }
                    // </FS:CR>

                    // <FS:CR> Show user name as legacy name if selected
                    std::string username( show_legacynames ? av_name.getUserNameForDisplay() : av_name.getAccountName() );

                    avatar.addNameTagLine(username, username_color, LLFontGL::NORMAL, LLFontGL::getFontSansSerifSmall(), true);
                }
// [RLVa:KB] - Checked: RLVa-1.2.2
            }
            else
            {
                avatar.addNameTagLine(RlvStrings::getAnonym(av_name), name_tag_color, LLFontGL::NORMAL, LLFontGL::getFontSansSerif(), true, (!av_name.getDisplayName().empty()) );
            }
// [/RLVa:KB]
        }
        else  // DISPLAY NAMES OFF
        {
            const LLFontGL* font = LLFontGL::getFontSansSerif();
            std::string full_name = LLCacheName::buildFullName( firstname->getString(), lastname->getString() );
// [RLVa:KB] - Checked: RLVa-1.2.2
            if ( (!fRlvShowAvName) && (!avatar.isSelf()) )
            {
                full_name = RlvStrings::getAnonym(full_name);
                avatar.addNameTagLine(full_name, name_tag_color, LLFontGL::NORMAL, font, true, true);
            }
// [/RLVa:KB]
            else // Only check for client tags when not RLV anon -AO
            {
                if (mClientTagData.has("name") && !mClientTagData["name"].asString().empty())
                {
                    avatar.addNameTagLine(full_name + " (" + mClientTagData["name"].asString() + ")", name_tag_color, LLFontGL::NORMAL, font, true, true);
                }
                else
                {
                    avatar.addNameTagLine(full_name, name_tag_color, LLFontGL::NORMAL, font, true, true);
                }
            }
        }

        // <FS:Ansariel> Show distance in tag
        if (show_distance_in_tag)
        {
            avatar.addNameTagLine(distance_string, distance_color, LLFontGL::NORMAL, LLFontGL::getFontSansSerifSmall());
        }
        // <FS:Ansariel> Show distance in tag

        // <FS:Ansariel> Show ARW in nametag options (for Jelly Dolls)
        static const std::string complexity_label = LLTrans::getString("Nametag_Complexity_Label");
        static const std::string texture_area_label = LLTrans::getString("Nametag_Texture_Area_Label");
        if (show_arw_tag &&
           ((avatar.isSelf() && show_own_arw_tag) ||
           (!avatar.isSelf() && (!show_too_complex_only_arw_tag || avatar.isTooComplex()))))
        {
            std::string complexity_string;
            LLLocale locale("");

            // always show complexity, even if the reason for a jelly baby is the texture area
            // this is technically not 100% correct but the decision logic with all of the
            // exceptions would be way too complex to justify the result - Zi
            LLResMgr::getInstance()->getIntegerString(complexity_string, complexity);
            LLStringUtil::format_map_t label_args;
            label_args["COMPLEXITY"] = complexity_string;

            avatar.addNameTagLine(FSCommon::format_string(complexity_label, label_args), complexity_color, LLFontGL::NORMAL, LLFontGL::getFontSansSerifSmall());

            // only show texture area if this is the reason for jelly baby rendering
            static LLCachedControl<F32> max_attachment_area(gSavedSettings, "RenderAutoMuteSurfaceAreaLimit", 1000.0f);
            if (max_attachment_area > 0.f && mAttachmentSurfaceArea > max_attachment_area)
            {
                LLResMgr::getInstance()->getIntegerString(complexity_string, (S32)mAttachmentSurfaceArea);
                label_args["TEXTURE_AREA"] = complexity_string;

                avatar.addNameTagLine(FSCommon::format_string(texture_area_label, label_args), LLColor4::red, LLFontGL::NORMAL, LLFontGL::getFontSansSerifSmall());
            }
        }
        // </FS:Ansariel>

        if (show_rez_status)
        {
            std::string av_string = LLVOAvatar::rezStatusToString(mLastRezzedStatus);
            avatar.addNameTagLine(av_string, name_tag_color, LLFontGL::NORMAL, LLFontGL::getFontSansSerifSmall(), true);
        }

        mNameAway = is_away;
        mNameDoNotDisturb = is_do_not_disturb;
        mNameAutoResponse = is_autoresponse; // <FS:Ansariel> Show auto-response in nametag
        mNameMute = is_muted;
        mNameAppearance = is_appearance;
        mNameFriend = is_friend;
        mNameCloud = is_cloud;
        mNameColor=name_tag_color;
        mDistanceString = distance_string;
        mTitle = title ? title->getString() : "";
        mNameIsTyping = is_typing;
        // <FS:Ansariel> FIRE-13414: Avatar name isn't updated when the simulator sends a new name
        mNameFirstname = firstname->getString();
        mNameLastname = lastname->getString();
        // </FS:Ansariel>
        // <FS:Ansariel> Show Arc in nametag (for Jelly Dolls)
        mNameArc = complexity;
        mNameArcColor = complexity_color;
        // </FS:Ansariel>
        LLStringFn::replace_ascii_controlchars(mTitle,LL_UNKNOWN_CHAR);
        new_name = true;
    }



    if (mVisibleChat || mVisibleTyping)
    {
        mNameText->setFont(LLFontGL::getFontSansSerif());
                mNameText->setTextAlignment(LLHUDNameTag::ALIGN_TEXT_LEFT);
        mNameText->setFadeDistance(CHAT_NORMAL_RADIUS * 2.f, 5.f);

        std::deque<LLChat>::iterator chat_iter = mChats.begin();
        mNameText->clearString();

        static const LLUIColor user_chat_color = LLUIColorTable::instance().getColor("UserChatColor");
        static const LLUIColor agent_chat_color = LLUIColorTable::instance().getColor("AgentChatColor");
        // <FS:CR> Colorize tags
        //const LLColor4& new_chat = avatar.isSelf() ? user_chat_color : agent_chat_color;
        LLColor4 new_chat = avatar.isSelf() ? user_chat_color : agent_chat_color;

        // <FS:CR> Colorize tags
        new_chat = LGGContactSets::getInstance()->colorize(avatar.getID(), new_chat, ContactSetType::CHAT);

        //color based on contact sets prefs
        LGGContactSets::getInstance()->hasFriendColorThatShouldShow(avatar.getID(), ContactSetType::CHAT, new_chat);
        // </FS:CR>

        if (mVisibleChat)
        {
        LLColor4 normal_chat = lerp(new_chat, LLColor4(0.8f, 0.8f, 0.8f, 1.f), 0.7f);
        LLColor4 old_chat = lerp(normal_chat, LLColor4(0.6f, 0.6f, 0.6f, 1.f), 0.7f);
        if (mTyping && mChats.size() >= MAX_BUBBLE_CHAT_UTTERANCES)
        {
            ++chat_iter;
        }

        for(; chat_iter != mChats.end(); ++chat_iter)
        {
            F32 chat_fade_amt = llclamp((F32)((LLFrameTimer::getElapsedSeconds() - chat_iter->mTime) / CHAT_FADE_TIME), 0.f, 4.f);
            LLFontGL::StyleFlags style;
            switch(chat_iter->mChatType)
            {
            case CHAT_TYPE_WHISPER:
                style = LLFontGL::ITALIC;
                break;
            case CHAT_TYPE_SHOUT:
                style = LLFontGL::BOLD;
                break;
            default:
                style = LLFontGL::NORMAL;
                break;
            }
            if (chat_fade_amt < 1.f)
            {
                F32 u = clamp_rescale(chat_fade_amt, 0.9f, 1.f, 0.f, 1.f);
                mNameText->addLine(chat_iter->mText, lerp(new_chat, normal_chat, u), style);
            }
            else if (chat_fade_amt < 2.f)
            {
                F32 u = clamp_rescale(chat_fade_amt, 1.9f, 2.f, 0.f, 1.f);
                mNameText->addLine(chat_iter->mText, lerp(normal_chat, old_chat, u), style);
            }
            else if (chat_fade_amt < 3.f)
            {
                // *NOTE: only remove lines down to minimum number
                mNameText->addLine(chat_iter->mText, old_chat, style);
            }
        }
        }
        mNameText->setVisibleOffScreen(true);

        if (mVisibleTyping && mTyping)
        {
            S32 dot_count = (llfloor(mTypingTimer.getElapsedTimeF32() * 3.f) + 2) % 3 + 1;
            switch(dot_count)
            {
            case 1:
                mNameText->addLine(".", new_chat);
                break;
            case 2:
                mNameText->addLine("..", new_chat);
                break;
            case 3:
                mNameText->addLine("...", new_chat);
                break;
            }

        }
    }
    else
    {
        // ...not using chat bubbles, just names
        mNameText->setTextAlignment(LLHUDNameTag::ALIGN_TEXT_CENTER);
        mNameText->setFadeDistance(CHAT_NORMAL_RADIUS, 5.f);
        mNameText->setVisibleOffScreen(false);
    }

    }
    template<class Owner>static void addLine(Owner& avatar,const std::string& line,const LLColor4& color,S32 style,const LLFontGL* font,bool use_ellipses,bool is_name)
    {
        auto& mNameIsSet=avatar.mNameIsSet;
        auto& mNameText=avatar.mNameText;
        auto& mVisibleChat=avatar.mVisibleChat;
        auto& mVisibleTyping=avatar.mVisibleTyping;

    // extra width (NAMETAG_MAX_WIDTH) is for names only, not for chat
    llassert(mNameText);
    if (mVisibleChat || mVisibleTyping)
    {
        mNameText->addLabel(line, LLHUDNameTag::NAMETAG_MAX_WIDTH);
    }
    else
    {
        mNameText->addLine(line, color, (LLFontGL::StyleFlags)style, font, use_ellipses, LLHUDNameTag::NAMETAG_MAX_WIDTH);
    }
    // <FS:Ansariel> Fix nametag not properly updating when display name arrives
    //mNameIsSet |= !line.empty();
    if (is_name)
    {
        mNameIsSet |= !line.empty();
    }
    // </FS:Ansariel>

    }
    template<class Owner>static void clear(Owner& avatar)
    {
        auto& mNameIsSet=avatar.mNameIsSet;
        auto& mNameText=avatar.mNameText;
        auto& mTimeVisible=avatar.mTimeVisible;

    mNameIsSet = false;
    if (mNameText)
    {
        mNameText->setLabel("");
        mNameText->setString("");
    }
    mTimeVisible.reset();

    }
    template<class Owner>static LLVector3 position(Owner& avatar,const LLVector3& root_pos_last)
    {
        auto& mAvatarOffset=avatar.mAvatarOffset;
        auto& mBodySize=avatar.mBodySize;
        auto& mCurRootToHeadOffset=avatar.mCurRootToHeadOffset;
        auto& mHeadp=avatar.mHeadp;
        auto& mNameText=avatar.mNameText;
        auto& mPelvisToFoot=avatar.mPelvisToFoot;
        auto& mRoot=avatar.mRoot;
        auto& mTargetRootToHeadOffset=avatar.mTargetRootToHeadOffset;

    LLQuaternion root_rot = mRoot->getWorldRotation();
    LLQuaternion inv_root_rot = ~root_rot;
    LLVector3 pixel_right_vec;
    LLVector3 pixel_up_vec;
    LLViewerCamera::getInstance()->getPixelVectors(root_pos_last, pixel_up_vec, pixel_right_vec);
    LLVector3 camera_to_av = root_pos_last - LLViewerCamera::getInstance()->getOrigin();
    camera_to_av.normalize();
    LLVector3 local_camera_at = camera_to_av * inv_root_rot;
    LLVector3 local_camera_up = camera_to_av % LLViewerCamera::getInstance()->getLeftAxis();
    local_camera_up.normalize();
    local_camera_up = local_camera_up * inv_root_rot;

    // <FS:Ansariel> Optional legacy nametag position
    LLVector3 name_position;
    static LLCachedControl<bool> fsLegacyNametagPosition(gSavedSettings, "FSLegacyNametagPosition");
    if (fsLegacyNametagPosition)
    {
        local_camera_up.scaleVec((mBodySize + mAvatarOffset) * 0.5f);
        local_camera_at.scaleVec((mBodySize + mAvatarOffset) * 0.5f);

        name_position = agentPosition(avatar,mRoot->getWorldPosition());
        name_position[VZ] -= mPelvisToFoot;
        name_position[VZ] += ((mBodySize[VZ] - mAvatarOffset[VZ] * 0.9f) * 0.55f);
        name_position += (local_camera_up * root_rot) - (projected_vec(local_camera_at * root_rot, camera_to_av));
        name_position += pixel_up_vec * 15.f;
    }
    else
    {
    // </FS:Ansariel>
    // position is based on head position, does not require mAvatarOffset here. - Nyx
    LLVector3 avatar_ellipsoid(mBodySize.mV[VX] * 0.4f,
                                mBodySize.mV[VY] * 0.4f,
                                mBodySize.mV[VZ] * NAMETAG_VERT_OFFSET_WEIGHT);

    local_camera_up.scaleVec(avatar_ellipsoid);
    local_camera_at.scaleVec(avatar_ellipsoid);

    LLVector3 head_offset = (mHeadp->getLastWorldPosition() - mRoot->getLastWorldPosition()) * inv_root_rot;

    if (dist_vec(head_offset, mTargetRootToHeadOffset) > NAMETAG_UPDATE_THRESHOLD)
    {
        mTargetRootToHeadOffset = head_offset;
    }

    mCurRootToHeadOffset = lerp(mCurRootToHeadOffset, mTargetRootToHeadOffset, LLSmoothInterpolation::getInterpolant(0.2f));

    // <FS:Ansariel> Optional legacy nametag position
    //LLVector3 name_position = agentPosition(avatar,mRoot->getLastWorldPosition()) + (mCurRootToHeadOffset * root_rot);
    name_position = agentPosition(avatar,mRoot->getLastWorldPosition()) + (mCurRootToHeadOffset * root_rot);
    name_position += (local_camera_up * root_rot) - (projected_vec(local_camera_at * root_rot, camera_to_av));
    name_position += pixel_up_vec * NAMETAG_VERTICAL_SCREEN_OFFSET;
    // <FS:Ansariel> Optional legacy nametag position
    }
    // </FS:Ansariel>

    // <FS:Ansariel> Optional Z-offset correction for name tags
    static LLCachedControl<S32> fsNameTagOffset(gSavedSettings, "FSNameTagZOffsetCorrection");
    name_position[VZ] += fsNameTagOffset / 10.f;
    // </FS:Ansariel>

    // Avoid of crossing the name tag by the water surface
    if (mNameText)
    {
        F32 water_height = waterHeight(avatar);
        static const F32 WATER_HEIGHT_ABOVE_DELTA = 0.25;
        if (name_position[VZ] < water_height + WATER_HEIGHT_ABOVE_DELTA)
        {
            F32 camera_height = LLViewerCamera::getInstance()->getOrigin()[VZ];
            if (camera_height >= water_height)
            {
                F32 name_world_height = mNameText->getWorldHeight();
                static const F32 WATER_HEIGHT_BELOW_DELTA = 0.5;
                if (name_position[VZ] + name_world_height > water_height - WATER_HEIGHT_BELOW_DELTA)
                {
                    name_position[VZ] = water_height + WATER_HEIGHT_ABOVE_DELTA;
                }
            }
        }
    }

    return name_position;

    }
    template<class Owner>static LLColor4 color(Owner& avatar)
    {

    // ...not using display names
    LLColor4 color = LLUIColorTable::getInstance()->getColor("NameTagLegacy");
    if (LLAvatarName::useDisplayNames())
    {
        // ...color based on whether username "matches" a computed display name
        LLAvatarName av_name;
        if (LLAvatarNameCache::get(avatar.getID(), &av_name) && av_name.isDisplayNameDefault())
        {
            color = LLUIColorTable::getInstance()->getColor("NameTagMatch");
        }
        else
        {
            color = LLUIColorTable::getInstance()->getColor("NameTagMismatch");
        }
    }

    // <FS:CR> FIRE-1061 - Color friends, lindens, muted, etc
    color = LGGContactSets::getInstance()->colorize(avatar.getID(), color, ContactSetType::TAG);
    // </FS:CR>

    LGGContactSets::getInstance()->hasFriendColorThatShouldShow(avatar.getID(), ContactSetType::TAG, color);

    LLNetMap::getAvatarMarkColor(avatar.getID(), color);

    return color;

    }
};
