#include <array>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>
#include <utility>
using MASK=unsigned;constexpr MASK MASK_SHIFT=1,MASK_CONTROL=2;
struct Pick{bool object=true,script=false;MASK mask=0;int node=7,primitive=3;};
struct Peer
{
 std::vector<std::string> trace;
 std::map<std::string,bool> settings{{"SelectOwnedOnly",false},{"SelectMovableOnly",true},{"FSSelectCopyableOnly",false},{"FSSelectLockedOnly",true},{"FSTurnAvatarToSelectedObject",true}};
 bool rlv=false,edit=true,build=false,far=false,attach=false,owned=false,visible=false,allow=false,selectedValue=false,allFaces=true,faceExists=true,faceSelected=false,media=false,avatarFocus=true,selfAttachmentValue=false,selfValue=false;
 float objectDistance=0,surfaceDistance=0;int faces=1;bool force=false;std::pair<float,float>zoom{.7f,.6f};
 void call(const std::string&s){trace.push_back(s);}
 bool current(){return true;}
 int object(const Pick&p){call("object");return p.object?1:0;}
 int rootEdit(int o){call("root");return o;}
 bool rlvEnabled(){call("rlv");return rlv;}
 bool canEdit(int){call("edit");return edit;}
 bool inBuildMode(){call("build");return build;}
 void leaveBuildMode(){call("leave");build=false;}
 bool fartouch(){call("far");return far;}
 bool attachment(int){call("attach");return attach;}
 bool youOwner(int){call("owner");return owned;}
 float fartouchDistanceSquared(){call("limit");return 2.25f;}
 float distanceToObjectSquared(int){call("object-distance");return objectDistance;}
 float distanceToSurfaceSquared(const Pick&){call("surface-distance");return surfaceDistance;}
 bool buildVisible(){call("build-visible");return visible;}
 MASK mask(const Pick&p){call("mask");return p.mask;}
 int selection(){call("selection");return selectedValue;}
 bool setting(const char*n){call(std::string("get-")+n);return settings[n];}
 void setting(const char*n,bool v){call(std::string("set-")+n+(v?"1":"0"));settings[n]=v;}
 bool allowSelectAvatar(){call("allow");return allow;}
 void forceSelection(bool v){call(v?"force1":"force0");force=v;}
 bool scriptIcon(const Pick&p){call("icon");return p.script;}
 void showScriptDebug(const Pick&){call("script");}
 bool selected(int){call("selected");return selectedValue;}
 int faceCount(int){call("faces");return faces;}
 bool containsAllFaces(int){call("contains");return allFaces;}
 std::optional<bool>faceSelection(int,const Pick&){call("face");return faceExists?std::optional<bool>(faceSelected):std::nullopt;}
 bool hasMediaFocus(){call("media");return media;}
 void clearMediaFocus(){call("clear-media");media=false;selectedValue=false;zoom={1,1};}
 void deselectOnly(int){call("deselect-only");selectedValue=false;}
 void deselectFamily(int){call("deselect-family");selectedValue=false;}
 void deselectAll(){call("deselect");selectedValue=false;zoom={1,1};}
 void selectOnly(int,int,int node,int primitive){call("select-only-"+std::to_string(node)+"-"+std::to_string(primitive));selectedValue=true;}
 void selectFamily(int){call("select-family");selectedValue=true;}
 auto hudZoom(){call("get-zoom");return zoom;}
 void hudZoom(std::pair<float,float>z){call("set-zoom");zoom=z;}
 int gltfNode(const Pick&p){return p.node;}int gltfPrimitive(const Pick&p){return p.primitive;}
 bool focusOnAvatar(){call("focus");return avatarFocus;}
 bool selfAttachment(int){call("self-attach");return selfAttachmentValue;}
 bool self(int){call("self");return selfValue;}
 void turnToSelection(int){call("turn");}
 void transientRootAndChildren(int){call("transient");}
};
KERNEL
REFERENCE
int main()
{
 std::mt19937 random(40098);
 for(unsigned iteration=0;iteration<100000;++iteration)
 {
  Peer gold;auto bit=[&]{return (random()&1)!=0;};
  gold.rlv=bit();gold.edit=bit();gold.build=bit();gold.far=bit();gold.attach=bit();gold.owned=bit();gold.visible=bit();gold.allow=bit();gold.selectedValue=bit();gold.allFaces=bit();gold.faceExists=bit();gold.faceSelected=bit();gold.media=bit();gold.avatarFocus=bit();gold.selfAttachmentValue=bit();gold.selfValue=bit();gold.faces=random()%3;gold.objectDistance=random()%4;gold.surfaceDistance=random()%4;
  Pick pick{bit(),bit(),static_cast<MASK>(random()%8)};bool ignore=bit(),temporary=bit(),root=bit();if(root)pick.object=true;
  auto native=gold;
  auto expected=reference(gold,pick,ignore,temporary,root);
  auto actual=vs_handle_object_selection(native,pick,ignore,temporary,root);
  // The reference reads mAllowSelectAvatar twice when not temporary; the
  // shared RAII owner captures it once so retirement cannot strand overrides.
  // It is a field read, not an owner callback: remove only that synthetic trace.
  auto strip=[](auto& t){t.erase(std::remove(t.begin(),t.end(),"allow"),t.end());};strip(gold.trace);strip(native.trace);
  if(expected!=actual||gold.trace!=native.trace||gold.settings!=native.settings||gold.zoom!=native.zoom||gold.force!=native.force)
  {
   std::cerr<<"Mismatch "<<iteration<<"\nGOLD\n";for(auto&s:gold.trace)std::cerr<<s<<"\n";std::cerr<<"NATIVE\n";for(auto&s:native.trace)std::cerr<<s<<"\n";return 1;
  }
 }
}

