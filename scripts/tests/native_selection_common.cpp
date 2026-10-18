unsigned bits=0;std::vector<std::string>calls;
bool field(const char*name,unsigned mask){calls.emplace_back(name);return bits&mask;}
struct LLViewerObject{
 static constexpr U8 LL_VO_SURFACE_PATCH=1;
 bool isDead(){return field("dead",8);}bool permYouOwner(){return field("owner",16);}bool permMove(){return field("move",32);}bool isPermanentEnforced(){return field("permanent",64);}bool permCopy(){return field("copy",128);}bool isOrphaned(){return field("orphan",256);}bool isAvatar(){return field("avatar",512);}U8 getPCode(){return field("pcode",1024)?1:0;}
};
struct Settings{bool getBOOL(const char*name){calls.push_back(name);return bits&(std::string(name)=="SelectOwnedOnly"?2048:std::string(name)=="SelectMovableOnly"?4096:std::string(name)=="FSSelectCopyableOnly"?8192:16384);}}gSavedSettings;
struct LLObjectSelection{ESelectType mSelectType=SELECT_TYPE_WORLD;int getObjectCount(){return field("count",32768)?1:0;}};
struct Handle{LLObjectSelection value;LLObjectSelection*operator->(){return &value;}LLObjectSelection*get(){return &value;}};
struct LLSelectMgr{bool mForceSelection=false;Handle mSelectedObjects;bool canSelectObject(LLViewerObject*,bool);private:ESelectType getSelectTypeForObject(LLViewerObject*){calls.push_back("selection_type");return SELECT_TYPE_HUD;}};
