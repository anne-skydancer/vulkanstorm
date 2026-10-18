glm::mat4 originalProjection(glm::mat4 proj_mat,float mZoomFactor,int mZoomSubregion,float fov_y,float aspect,float z_near,float z_far)
{
    if (mZoomFactor > 1.f)
    {
        float offset = mZoomFactor - 1.f;
        int pos_y = mZoomSubregion / llceil(mZoomFactor);
        int pos_x = mZoomSubregion - (pos_y*llceil(mZoomFactor));

        glm::mat4 translate;
        translate = glm::translate(glm::vec3(offset - (F32)pos_x * 2.f, offset - (F32)pos_y * 2.f, 0.f));
        glm::mat4 scale;
        scale = glm::scale(glm::vec3(mZoomFactor, mZoomFactor, 1.f));

        proj_mat = scale * proj_mat;
        proj_mat = translate * proj_mat;
    }

    proj_mat *= glm::perspective(fov_y,aspect,z_near,z_far);
    return proj_mat;
}
