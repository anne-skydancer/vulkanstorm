virtual bool apply(LLSelectNode* selectNode)
        {
            LLViewerObject* object = selectNode->getObject();
            if (object && object->permMove() && !object->isPermanentEnforced())
            {
                if (!selectNode->mLastPositionLocal.isExactlyZero())
                {
                    object->setPosition(selectNode->mLastPositionLocal);
                }
                if (selectNode->mLastRotation != LLQuaternion())
                {
                    object->setRotation(selectNode->mLastRotation);
                }
                if (!selectNode->mLastScale.isExactlyZero())
                {
                    object->setScale(selectNode->mLastScale);
                }
            }
            return true;
        }