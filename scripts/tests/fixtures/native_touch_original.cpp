void handle_object_touch()
{
    LLViewerObject* object = LLSelectMgr::getInstance()->getSelection()->getPrimaryObject();
    if (!object) return;

    LLPickInfo pick = LLToolPie::getInstance()->getPick();

// [RLVa:KB] - Checked: 2010-04-11 (RLVa-1.2.0e) | Modified: RLVa-1.1.0l
    // NOTE: fallback code since we really shouldn't be getting an active selection if we can't touch this
    if ( (RlvActions::isRlvEnabled()) && (!RlvActions::canTouch(object, pick.mObjectOffset)) )
    {
        RLV_ASSERT(false);
        return;
    }
// [/RLVa:KB]

    // *NOTE: Hope the packets arrive safely and in order or else
    // there will be some problems.
    // *TODO: Just fix this bad assumption.
    send_ObjectGrab_message(object, pick, LLVector3::zero);
    send_ObjectDeGrab_message(object, pick);
}