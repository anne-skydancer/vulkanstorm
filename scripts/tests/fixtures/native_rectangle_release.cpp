#include <cassert>
#include <functional>
#include <memory>
using S32=int;using MASK=int;constexpr MASK MASK_CONTROL=4;
struct Window{bool native=true;bool isNativeVulkan(){return native;}}window;auto*gViewerWindow=&window;
struct Selection{bool highlights=true;int commits=0,clears=0;bool subtract=false;
 bool commitRectangle(bool sub,const std::function<bool()>&current){if(!current||!current())return false;assert(highlights);subtract=sub;highlights=false;++commits;return true;}
 void clearRectangle(){highlights=false;++clears;}
};
struct VSNativeSession{std::shared_ptr<Selection>selection=std::make_shared<Selection>();static inline std::shared_ptr<VSNativeSession>activeSession;
 static auto active(){return activeSession;}auto retainObjectSelection(){return selection;}auto*existingObjectSelection(){return selection.get();}};
struct LLSelectMgr{static LLSelectMgr*getInstance(){static LLSelectMgr s;return &s;}void deselectHighlightedObjects(){}void selectHighlightedObjects(){}};
struct LLToolSelect{bool handleMouseUp(S32,S32,MASK){return false;}void handleDeselect(){}};
struct LLToolSelectRect:LLToolSelect{
 bool mMouseOutsideSlop=true,mNativeRectangleReleasing=false;S32 mDragLastWidth=3,mDragLastHeight=4;
 std::function<bool()>mNativeRectangleCurrent=[]{return true;};bool transitionOnRelease=true;
 void setMouseCapture(bool){if(transitionOnRelease)handleDeselect();}
 bool handleMouseUp(S32,S32,MASK);void handleDeselect();
};
UP
DESELECT
int main(){VSNativeSession::activeSession=std::make_shared<VSNativeSession>();auto selection=VSNativeSession::activeSession->selection;
 LLToolSelectRect rect;assert(rect.handleMouseUp(1,2,0));assert(selection->commits==1&&selection->clears==0&&!rect.mNativeRectangleReleasing);
 selection->highlights=true;rect.mMouseOutsideSlop=true;assert(rect.handleMouseUp(1,2,MASK_CONTROL));assert(selection->commits==2&&selection->subtract);
 selection->highlights=true;rect.handleDeselect();assert(selection->clears==1&&!selection->highlights&&!rect.mNativeRectangleCurrent);
 selection->highlights=true;rect.mMouseOutsideSlop=true;rect.mNativeRectangleCurrent=[]{return false;};assert(rect.handleMouseUp(1,2,0));assert(selection->commits==2&&selection->highlights);
}
