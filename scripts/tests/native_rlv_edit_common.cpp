unsigned bits=0;std::vector<std::string>calls;
struct LLViewerObject{
 LLUUID id=17;const LLViewerObject*getRootEdit()const{calls.push_back("root");return this;}const LLUUID&getID()const{calls.push_back("id");return id;}
 bool isAttachment()const{calls.push_back("attachment");return bits&64;}bool isHUDAttachment()const{calls.push_back("hud");return bits&128;}LLVector3d getPositionGlobal()const{calls.push_back("position");return {bits&256?10.f:1.f,0,0};}
};
struct VSObjectContext{LLUUID rootEditID=17;bool attachment=false,hud=false;LLVector3d position;std::function<bool()>current;};
struct RlvHandler{
 static RlvHandler&instance(){static RlvHandler h;return h;}bool hasBehaviour(int flag){calls.push_back("behaviour:"+std::to_string(flag));return bits&flag;}
 bool isException(int flag,LLUUID id){calls.push_back("exception:"+std::to_string(flag)+":"+std::to_string(id));return bits&(flag==RLV_BHVR_EDIT?512:1024);}
};
template<class T>struct RlvCachedBehaviourModifier{explicit RlvCachedBehaviourModifier(int){}operator T()const{return 2.f;}};
struct Agent{LLVector3d getPositionGlobal()const{calls.push_back("agent_position");return {};}}gAgent;
struct RlvActions{static bool canEdit(const LLViewerObject*);static bool canEdit(const VSObjectContext&);static bool canInteract(const LLViewerObject*,const LLVector3&);static bool canInteract(const VSObjectContext&,const LLVector3&);};
