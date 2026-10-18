void LLSelectMgr::packMultipleUpdate(LLSelectNode* node, void *user_data)
{
    LLViewerObject* object = node->getObject();
    U32 *type32 = (U32 *)user_data;
    U8 type = (U8)*type32;
    U8  data[256];

    gMessageSystem->nextBlockFast(_PREHASH_ObjectData);
    gMessageSystem->addU32Fast(_PREHASH_ObjectLocalID,  object->getLocalID() );
    gMessageSystem->addU8Fast(_PREHASH_Type, type );

    S32 offset = 0;

    // JC: You MUST pack the data in this order.  The receiving
    // routine process_multiple_update_message on simulator will
    // extract them in this order.

    if (type & UPD_POSITION)
    {
        htolememcpy(&data[offset], &(object->getPosition().mV), MVT_LLVector3, 12);
        offset += 12;
    }
    if (type & UPD_ROTATION)
    {
        LLQuaternion quat = object->getRotation();
        LLVector3 vec = quat.packToVector3();
        htolememcpy(&data[offset], &(vec.mV), MVT_LLQuaternion, 12);
        offset += 12;
    }
    if (type & UPD_SCALE)
    {
        //LL_INFOS() << "Sending object scale " << object->getScale() << LL_ENDL;
        htolememcpy(&data[offset], &(object->getScale().mV), MVT_LLVector3, 12);
        offset += 12;
    }
    gMessageSystem->addBinaryDataFast(_PREHASH_Data, data, offset);
}