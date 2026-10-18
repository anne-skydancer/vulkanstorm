const LLMatrix4 LLViewerObject::getRenderMatrix() const
{
    return mDrawable->getWorldMatrix();
}