// Frozen original OpenGL/reference picking implementation; LGPL-2.1.
bool OriginalAvatar::intersect(const LLVector4a&start,const LLVector4a&end,LLVector4a*intersection,LLVector4a*normal){
for (S32 i = 0; i < mNumCollisionVolumes; ++i)
        {
            mCollisionVolumes[i].updateWorldMatrix();

            glm::mat4 mat(glm::make_mat4((F32*) mCollisionVolumes[i].getXform()->getWorldMatrix().mMatrix));
            glm::mat4 inverse = glm::inverse(mat);
            glm::mat4 norm_mat = glm::transpose(inverse);

            glm::vec3 p1(start);
            glm::vec3 p2(end);

            p1 = mul_mat4_vec3(inverse, p1);
            p2 = mul_mat4_vec3(inverse, p2);

            LLVector3 position;
            LLVector3 norm;

            if (linesegment_sphere(LLVector3(p1), LLVector3(p2), LLVector3(0,0,0), 1.f, position, norm))
            {
                glm::vec3 res_pos(position);
                res_pos = mul_mat4_vec3(mat, res_pos);

                 glm::vec3 res_norm(norm);
                res_norm = glm::normalize(res_norm);
                res_norm = glm::mat3(norm_mat) * res_norm;

                if (intersection)
                {
                    intersection->load3(glm::value_ptr(res_pos));
                }

                if (normal)
                {
                    normal->load3(glm::value_ptr(res_norm));
                }

                return true;
            }
        }
return false;
}
