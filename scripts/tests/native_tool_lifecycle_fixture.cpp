#include <vector>
#include <functional>
#include <cassert>
#include <cstdint>
#include <iostream>
using U64=uint64_t;using S32=int;using MASK=unsigned;
#define LL_WARNS() std::cerr
#define LL_ENDL '\n'
class LLToolMgr;class LLTool;
LLToolMgr*manager=nullptr;LLTool*overrideTool=nullptr;
struct Keyboard{MASK currentMask(bool){return 0;}}keyboard;Keyboard*gKeyboard=&keyboard;
class LLTool
{
public:
 bool capture=false;unsigned selects=0,deselects=0;std::function<void()>onDeselect,onRelease;
 virtual ~LLTool()=default;virtual LLTool*getOverrideTool(MASK){return overrideTool;}
 virtual void handleSelect(){++selects;}virtual void handleDeselect(){++deselects;if(onDeselect)onDeselect();}
 bool hasMouseCapture(){return capture;}void setMouseCapture(bool value){capture=value;if(!value&&onRelease)onRelease();}
};
#define TOOL(name) class name:public LLTool{public:static name*getInstance(){static name value;return &value;}};
TOOL(LLToolPie) TOOL(LLToolCamera) TOOL(LLToolGrab) TOOL(LLToolCompGun)
class LLToolCompInspect:public LLTool{public:bool active=false;bool isToolCameraActive(){return active;}static LLToolCompInspect*getInstance(){static LLToolCompInspect value;return &value;}};
struct LLFloaterInspect{bool getVisible(){return false;}};struct LLFloaterReg{template<class T>static T*getTypedInstance(const char*){return nullptr;}};
class LLToolset
{
public:
 std::vector<LLTool*>tools;LLTool*selected=nullptr;
 void addTool(LLTool*t){tools.push_back(t);if(!selected)selected=t;}
 void selectTool(LLTool*);void selectFirstTool(){selectTool(tools.empty()?nullptr:tools.front());}
 LLTool*getSelectedTool(){return selected;}
};
LLTool nullTool;LLTool*gToolNull=&nullTool;LLToolset basic,camera,mouselook,face;LLToolset*gBasicToolset=&basic,*gCameraToolset=&camera,*gMouselookToolset=&mouselook,*gFaceEditToolset=&face;
class LLToolMgr
{
public:
 bool mNativeToolsInitialized=false,mNativeToolsReady=false;U64 mNativeToolGeneration=0;
 LLTool*mBaseTool=gToolNull,*mSavedTool=nullptr,*mTransientTool=nullptr,*mOverrideTool=nullptr,*mSelectedTool=gToolNull;LLToolset*mCurrentToolset=nullptr;
 void initNativeTools();void retireNativeTools();bool nativeToolsReady()const;LLTool*nativeCurrentTool();LLTool*getCurrentTool();void setCurrentToolset(LLToolset*);void setCurrentTool(LLTool*);void updateToolStatus();void setTransientTool(LLTool*);void clearTransientTool();
};
void LLToolset::selectTool(LLTool*t){selected=t;manager->setCurrentTool(t);}
METHODS
int main()
{
 LLToolMgr owner;manager=&owner;assert(!owner.nativeCurrentTool());owner.initNativeTools();assert(owner.nativeToolsReady()&&owner.nativeCurrentTool()==LLToolPie::getInstance());assert(basic.tools.size()==4&&mouselook.tools.size()==1);
 overrideTool=LLToolCamera::getInstance();assert(owner.nativeCurrentTool()==overrideTool);auto*cameraTool=LLToolCamera::getInstance();cameraTool->capture=true;overrideTool=LLToolGrab::getInstance();assert(owner.nativeCurrentTool()==cameraTool);cameraTool->capture=false;assert(owner.nativeCurrentTool()==LLToolGrab::getInstance());
 LLTool transient;owner.setTransientTool(&transient);assert(owner.nativeCurrentTool()==&transient);owner.clearTransientTool();overrideTool=nullptr;assert(owner.nativeCurrentTool()==LLToolPie::getInstance());
 auto*pie=LLToolPie::getInstance();pie->onDeselect=[&]{owner.retireNativeTools();};overrideTool=cameraTool;const auto selectedBefore=cameraTool->selects;assert(owner.nativeCurrentTool()==nullptr);assert(!owner.nativeToolsReady()&&cameraTool->selects==selectedBefore);pie->onDeselect={};
 overrideTool=nullptr;owner.initNativeTools();assert(basic.tools.size()==4&&owner.nativeCurrentTool()==pie);
 pie->capture=true;pie->onRelease=[&]{owner.initNativeTools();};const auto deselects=pie->deselects;owner.retireNativeTools();assert(owner.nativeToolsReady()&&owner.nativeCurrentTool()==pie&&pie->deselects==deselects);pie->onRelease={};
 owner.retireNativeTools();assert(!owner.nativeCurrentTool()&&owner.getCurrentTool()==gToolNull);
}
