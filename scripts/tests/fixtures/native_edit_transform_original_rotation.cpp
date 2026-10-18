const LLQuaternion LLViewerObject::getRotationEdit() const
{
    LLQuaternion global_rotation = getRotation();
    if (!((LLXform *)this)->isRootEdit())
    {
        global_rotation = global_rotation * getParent()->getRotation();
    }
    return global_rotation;
}