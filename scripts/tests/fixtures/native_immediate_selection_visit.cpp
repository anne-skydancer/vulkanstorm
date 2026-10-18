#include <cassert>
#include <functional>
#include <memory>
#include <vector>
using U64=unsigned long long;
struct VSWorldScene{struct ObjectHandle{int id;};};
struct Session:std::enable_shared_from_this<Session>{bool alive=true;auto accountGuard(){return [owner=shared_from_this()]{return owner->alive;};}};
struct VSObjectSelection{
 struct NodeMetadata{int faceMask;};
 struct State{struct Node{VSWorldScene::ObjectHandle object;NodeMetadata metadata;};std::weak_ptr<Session>session;bool retired=false;U64 revision=1;std::vector<Node>nodes;bool current(VSWorldScene::ObjectHandle t){return t.id!=2;}};
 std::shared_ptr<State>mImpl=std::make_shared<State>();
 bool visitSelected(const std::function<bool(const VSWorldScene::ObjectHandle&,const NodeMetadata&)>&)const;
};
ACTUAL
int main(){VSObjectSelection selection;auto session=std::make_shared<Session>();selection.mImpl->session=session;selection.mImpl->nodes={{{1},{4}},{{2},{5}},{{3},{7}}};
 std::vector<int>seen;assert(selection.visitSelected([&](const auto&t,const auto&m){seen.push_back(t.id*10+m.faceMask);return true;}));assert(seen==std::vector<int>({14,37}));
 int calls=0;assert(!selection.visitSelected([&](const auto&,const auto&){++calls;return false;}));assert(calls==1);
 calls=0;assert(!selection.visitSelected([&](const auto&,const auto&){++calls;selection.mImpl->nodes.clear();++selection.mImpl->revision;return true;}));assert(calls==1);
 selection.mImpl->nodes={{{1},{4}}};assert(!selection.visitSelected([&](const auto&,const auto&){session->alive=false;return true;}));session->alive=true;
 assert(!selection.visitSelected([&](const auto&,const auto&){selection.mImpl->retired=true;return true;}));
}
