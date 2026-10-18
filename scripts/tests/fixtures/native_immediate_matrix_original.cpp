void ReferenceMatrix::rotatef(const GLfloat& a, const GLfloat& x, const GLfloat& y, const GLfloat& z)
{
    flush();

    {
        mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] = glm::rotate(mMatrix[mMatrixMode][mMatIdx[mMatrixMode]], glm::radians(a), glm::vec3(x,y,z));
        mMatHash[mMatrixMode]++;
    }
}
void ReferenceMatrix::translatef(const GLfloat& x, const GLfloat& y, const GLfloat& z)
{
    flush();

    {
        mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] = glm::translate(mMatrix[mMatrixMode][mMatIdx[mMatrixMode]], glm::vec3(x, y, z));
        mMatHash[mMatrixMode]++;
    }
}
void ReferenceMatrix::scalef(const GLfloat& x, const GLfloat& y, const GLfloat& z)
{
    flush();

    {
        mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] = glm::scale(mMatrix[mMatrixMode][mMatIdx[mMatrixMode]], glm::vec3(x, y, z));
        mMatHash[mMatrixMode]++;
    }
}
void ReferenceMatrix::multMatrix(const GLfloat* m)
{
    flush();
    {
        mMatrix[mMatrixMode][mMatIdx[mMatrixMode]] *= glm::make_mat4(m);
        mMatHash[mMatrixMode]++;
    }
}