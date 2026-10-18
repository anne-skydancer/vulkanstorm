void ReferenceVertexColor::color4f(const GLfloat& r, const GLfloat& g, const GLfloat& b, const GLfloat& a)
{
    color4ub((GLubyte) (llclamp(r, 0.f, 1.f)*255),
        (GLubyte) (llclamp(g, 0.f, 1.f)*255),
        (GLubyte) (llclamp(b, 0.f, 1.f)*255),
        (GLubyte) (llclamp(a, 0.f, 1.f)*255));
}