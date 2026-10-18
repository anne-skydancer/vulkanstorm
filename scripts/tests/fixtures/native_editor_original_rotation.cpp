const LLQuaternion LLViewerObject::getRotationRegion() const
{
    LLQuaternion global_rotation = getRotation();
    if (!isRoot())
    {
        global_rotation = global_rotation * getParent()->getRotation();
    }
    return global_rotation;
}