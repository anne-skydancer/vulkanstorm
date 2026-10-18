#include <memory>
#include <optional>
#include <vector>
#include <cassert>
struct VSNativeSession{enum class Phase{Connected,Disconnected};Phase phaseValue=Phase::Connected;Phase phase(){return phaseValue;}};
struct VSObjectSelection{struct Node{bool object=true,pending=false;std::optional<int>publication=1;};struct State{bool retired=false;std::weak_ptr<VSNativeSession>session;std::vector<Node>nodes;bool current(bool valid){return valid;}};std::shared_ptr<State>mImpl=std::make_shared<State>();bool selectGetAllValid()const;};
METHOD
int main(){VSObjectSelection selection;auto session=std::make_shared<VSNativeSession>();selection.mImpl->session=session;assert(selection.selectGetAllValid());selection.mImpl->nodes.push_back({});assert(selection.selectGetAllValid());for(int mask=0;mask<16;++mask){selection.mImpl->nodes[0]={!(mask&1),bool(mask&2),(mask&4)?std::optional<int>{}:std::optional<int>{1}};selection.mImpl->retired=mask&8;assert(selection.selectGetAllValid()==(mask==0));}selection.mImpl->retired=false;selection.mImpl->nodes.clear();session->phaseValue=VSNativeSession::Phase::Disconnected;assert(!selection.selectGetAllValid());session.reset();assert(!selection.selectGetAllValid());}
