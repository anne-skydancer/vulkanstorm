void LLViewerCamera::updateFrustumPlanes(LLCamera& camera, bool ortho, bool zflip, bool no_hacks)
{
    glm::ivec4 viewport = glm::make_vec4((GLint*) gGLViewport);
    glm::mat4 model = get_current_modelview();
    glm::mat4 proj = get_current_projection();

    LLVector3 frust[8];

    glm::vec3 obj;
    if (no_hacks)
    {
        obj = glm::unProject(glm::vec3(viewport[0], viewport[1], 0), model, proj, viewport);
        frust[0].setVec(glm::value_ptr(obj));
        obj = glm::unProject(glm::vec3(viewport[0]+viewport[2],viewport[1],0),model,proj,viewport);
        frust[1].setVec(glm::value_ptr(obj));
        obj = glm::unProject(glm::vec3(viewport[0]+viewport[2],viewport[1]+viewport[3],0),model,proj,viewport);
        frust[2].setVec(glm::value_ptr(obj));
        obj = glm::unProject(glm::vec3(viewport[0],viewport[1]+viewport[3],0),model,proj,viewport);
        frust[3].setVec(glm::value_ptr(obj));

        obj = glm::unProject(glm::vec3(viewport[0],viewport[1],1),model,proj,viewport);
        frust[4].setVec(glm::value_ptr(obj));
        obj = glm::unProject(glm::vec3(viewport[0]+viewport[2],viewport[1],1),model,proj,viewport);
        frust[5].setVec(glm::value_ptr(obj));
        obj = glm::unProject(glm::vec3(viewport[0]+viewport[2],viewport[1]+viewport[3],1),model,proj,viewport);
        frust[6].setVec(glm::value_ptr(obj));
        obj = glm::unProject(glm::vec3(viewport[0],viewport[1]+viewport[3],1),model,proj,viewport);
        frust[7].setVec(glm::value_ptr(obj));
    }
    else if (zflip)
    {
        obj = glm::unProject(glm::vec3(viewport[0],viewport[1]+viewport[3],0),model,proj,viewport);
        frust[0].setVec(glm::value_ptr(obj));
        obj = glm::unProject(glm::vec3(viewport[0]+viewport[2],viewport[1]+viewport[3],0),model,proj,viewport);
        frust[1].setVec(glm::value_ptr(obj));
        obj = glm::unProject(glm::vec3(viewport[0]+viewport[2],viewport[1],0),model,proj,viewport);
        frust[2].setVec(glm::value_ptr(obj));
        obj = glm::unProject(glm::vec3(viewport[0],viewport[1],0),model,proj,viewport);
        frust[3].setVec(glm::value_ptr(obj));

        obj = glm::unProject(glm::vec3(viewport[0],viewport[1]+viewport[3],1),model,proj,viewport);
        frust[4].setVec(glm::value_ptr(obj));
        obj = glm::unProject(glm::vec3(viewport[0]+viewport[2],viewport[1]+viewport[3],1),model,proj,viewport);
        frust[5].setVec(glm::value_ptr(obj));
        obj = glm::unProject(glm::vec3(viewport[0]+viewport[2],viewport[1],1),model,proj,viewport);
        frust[6].setVec(glm::value_ptr(obj));
        obj = glm::unProject(glm::vec3(viewport[0],viewport[1],1),model,proj,viewport);
        frust[7].setVec(glm::value_ptr(obj));

        for (U32 i = 0; i < 4; i++)
        {
            frust[i+4] = frust[i+4]-frust[i];
            frust[i+4].normVec();
            frust[i+4] = frust[i] + frust[i+4]*camera.getFar();
        }
    }
    else
    {
        obj = glm::unProject(glm::vec3(viewport[0],viewport[1],0),model,proj,viewport);
        frust[0].setVec(glm::value_ptr(obj));
        obj = glm::unProject(glm::vec3(viewport[0]+viewport[2],viewport[1],0),model,proj,viewport);
        frust[1].setVec(glm::value_ptr(obj));
        obj = glm::unProject(glm::vec3(viewport[0]+viewport[2],viewport[1]+viewport[3],0),model,proj,viewport);
        frust[2].setVec(glm::value_ptr(obj));
        obj = glm::unProject(glm::vec3(viewport[0],viewport[1]+viewport[3],0),model,proj,viewport);
        frust[3].setVec(glm::value_ptr(obj));

        if (ortho)
        {
            LLVector3 far_shift = camera.getAtAxis()*camera.getFar()*2.f;
            for (U32 i = 0; i < 4; i++)
            {
                frust[i+4] = frust[i] + far_shift;
            }
        }
        else
        {
            for (U32 i = 0; i < 4; i++)
            {
                LLVector3 vec = frust[i] - camera.getOrigin();
                vec.normVec();
                frust[i+4] = camera.getOrigin() + vec*camera.getFar();
            }
        }
    }

    camera.calcAgentFrustumPlanes(frust);
}