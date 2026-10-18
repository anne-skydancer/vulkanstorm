    mXform.setPosition(target_pos);
    mXform.setRotation(target_rot);
    mXform.setScale(LLVector3(1,1,1)); //no scale in drawable transforms (IT'S A RULE!)
    mXform.updateMatrix();
