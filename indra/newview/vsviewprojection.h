// Shared original viewer crop/zoom/perspective policy. LGPL-2.1.
#pragma once
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/transform.hpp>

// The original viewer projection order is significant: crop first, then
// snapshot zoom/subregion, and finally the perspective projection.
inline glm::mat4 vs_pick_projection(const glm::ivec4& viewport,int x,int y,int width,int height)
{
    return glm::pickMatrix(glm::vec2(x+width/2.f,y+height/2.f),glm::vec2(float(width),float(height)),viewport);
}
inline glm::mat4 vs_view_projection(glm::mat4 projection,float zoom,int subregion,
                                    float view,float aspect,float nearPlane,float farPlane)
{
    if(zoom>1.f)
    {
        float offset=zoom-1.f;
        int pos_y=subregion/int(std::ceil(zoom));
        int pos_x=subregion-(pos_y*int(std::ceil(zoom)));
        glm::mat4 translate=glm::translate(glm::vec3(offset-float(pos_x)*2.f,offset-float(pos_y)*2.f,0.f));
        glm::mat4 scale=glm::scale(glm::vec3(zoom,zoom,1.f));
        projection=scale*projection;
        projection=translate*projection;
    }
    projection*=glm::perspective(view,aspect,nearPlane,farPlane);
    return projection;
}
