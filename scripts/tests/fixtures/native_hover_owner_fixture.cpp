#include <memory>
#include <optional>
#include <vector>
#include <cassert>
struct VSWorldScene{struct ObjectHandle{int id=0,generation=0;};};
struct VSObjectSelection{struct Impl{struct Node{VSWorldScene::ObjectHandle object;};bool currentAccount=true;std::vector<Node>hover;int currentGeneration=3;bool current(VSWorldScene::ObjectHandle token){return currentAccount&&token.generation==currentGeneration;}};std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();std::optional<VSWorldScene::ObjectHandle>hoverObject()const;};
METHOD
int main(){VSObjectSelection owner;assert(!owner.hoverObject());owner.mImpl->hover={{{1,2}},{{2,3}},{{3,3}}};assert(owner.hoverObject()->id==2);owner.mImpl->currentAccount=false;assert(!owner.hoverObject());owner.mImpl->currentAccount=true;owner.mImpl->currentGeneration=4;assert(!owner.hoverObject());owner.mImpl->hover[0].object={1,4};assert(owner.hoverObject()->id==1);}
