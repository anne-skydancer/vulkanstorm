#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <cassert>
using S32=int;struct Matrix{float value;};namespace glm{const float*value_ptr(const Matrix&m){return&m.value;}}
struct LLMatrix4a{float value=0;void loadu(const float*p){value=*p;}};
struct VSWorldGLTF{struct Node{Matrix mAssetMatrix;};struct Asset{std::vector<Node>mNodes;};struct Impl{std::function<bool()>current;bool ready=true;std::string error;std::shared_ptr<Asset>asset;};std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();bool nodeMatrix(S32,LLMatrix4a&)const;};
METHOD
int main(){VSWorldGLTF world;bool current=true;world.mImpl->current=[&]{return current;};LLMatrix4a result{99};assert(!world.nodeMatrix(0,result)&&result.value==99);world.mImpl->asset=std::make_shared<VSWorldGLTF::Asset>();world.mImpl->asset->mNodes={{{7}},{{11}}};assert(world.nodeMatrix(1,result)&&result.value==11);result.value=33;assert(!world.nodeMatrix(-1,result)&&!world.nodeMatrix(2,result)&&result.value==33);world.mImpl->ready=false;assert(!world.nodeMatrix(0,result));world.mImpl->ready=true;world.mImpl->error="bad";assert(!world.nodeMatrix(0,result));world.mImpl->error.clear();current=false;assert(!world.nodeMatrix(0,result)&&result.value==33);}
