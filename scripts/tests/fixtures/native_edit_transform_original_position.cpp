const LLVector3 LLViewerObject::getPositionEdit() const
{
    if (isRootEdit())
    {
        return getPosition();
    }
    else
    {
        LLViewerObject *parent = (LLViewerObject *)getParent();
        LLVector3 position_edit = parent->getPositionEdit() + getPosition() * parent->getRotationEdit();
        return position_edit;
    }
}