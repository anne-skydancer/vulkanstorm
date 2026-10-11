
#include <algorithm>
#include <cassert>
#include <iterator>
#include <iostream>
#include <memory>
#include <set>
#include <vector>
using S32=int; using MASK=unsigned;
struct LLView;
template<class T> struct LLHandle {
 T* ptr=nullptr; std::shared_ptr<bool> alive;
 T* get() const { return alive && *alive ? ptr : nullptr; }
 bool operator<(const LLHandle& other) const { return std::less<T*>()(ptr,other.ptr); }
};
struct LLRect {
 int mLeft=0,mBottom=0,mRight=100,mTop=100;
 bool pointInRect(int x,int y) const { return x>=mLeft && x<mRight && y>=mBottom && y<mTop; }
 int getCenterX()const{return (mLeft+mRight)/2;}int getCenterY()const{return (mBottom+mTop)/2;}
 int getHeight()const{return mTop-mBottom;}
 void translate(int x,int y){mLeft+=x;mRight+=x;mBottom+=y;mTop+=y;}
};
struct LLMouseHandler {
 virtual ~LLMouseHandler()=default;
 virtual void screenPointToLocal(int x,int y,int* ox,int* oy) { *ox=x;*oy=y; }
 virtual bool handleHover(int,int,MASK) { return false; }
};
struct LLView:LLMouseHandler {
 LLView* parent=nullptr; std::vector<LLView*> children; LLRect rect;
 bool visible=true,opaque=false,highlight=false; unsigned entered=0,left=0;
 std::shared_ptr<bool> alive=std::make_shared<bool>(true);
 LLHandle<LLView> getHandle() { return {this,alive}; }
 LLView* getParent() { return parent; }
 bool getVisible() const { return visible; }
 bool pointInView(int x,int y) const { return x>=0 && y>=0 && x<rect.mRight-rect.mLeft && y<rect.mTop-rect.mBottom; }
 void screenPointToLocal(int x,int y,int* ox,int* oy) override { *ox=x-rect.mLeft;*oy=y-rect.mBottom; }
 bool handleHover(int x,int y,MASK mask) override {
  for(auto* child:children) { int cx=x+rect.mLeft-child->rect.mLeft,cy=y+rect.mBottom-child->rect.mBottom;
   if(child->visible && child->pointInView(cx,cy) && child->handleHover(cx,cy,mask)) return true;
  }
  if(parent) { highlight=true; return true; } return false;
 }
 bool getMouseOpaque() const { return opaque; }
 LLRect calcScreenBoundingRect() const { return rect; }
 LLRect calcScreenRect() const { return rect; }
 LLRect getRect()const{return rect;}void setShape(LLRect value){rect=value;}
 bool getHighlight()const{return highlight;}
 void onMouseEnter(int,int,MASK) { ++entered; highlight=true; }
 void onMouseLeave(int,int,MASK) { ++left; highlight=false; }
 void add(LLView& child) { child.parent=this; children.push_back(&child); }
 struct tree_iterator_t {
  std::shared_ptr<std::vector<std::pair<LLView*,int>>> entries; size_t index=0;
  bool end() const { return !entries || index>=entries->size(); }
  LLView* operator*() const { return (*entries)[index].first; }
  tree_iterator_t& operator++() { ++index; return *this; }
  bool operator!=(const tree_iterator_t& other) const { return !(end() && other.end()); }
  void skipDescendants() { const int depth=(*entries)[index].second;
   while(index+1<entries->size() && (*entries)[index+1].second>depth) ++index; }
 };
 static void flatten(LLView* v,int depth,std::vector<std::pair<LLView*,int>>& out) {
  out.emplace_back(v,depth); for(auto* child:v->children) flatten(child,depth+1,out);
 }
 tree_iterator_t beginTreeDFS() { auto out=std::make_shared<std::vector<std::pair<LLView*,int>>>(); flatten(this,0,*out); return {out,0}; }
 tree_iterator_t endTreeDFS() { return {}; }
};
using LLUICtrl=LLView;
struct Focus { LLUICtrl* top=nullptr; LLMouseHandler* capture=nullptr;
 LLUICtrl* getTopCtrl() { return top; } LLMouseHandler* getMouseCapture() { return capture; }
} gFocusMgr;
struct LLPopupView { using popup_list_t=std::vector<LLHandle<LLView>>; popup_list_t popups;
 popup_list_t getCurrentPopups() { return popups; }
};
struct LLPipeline { enum {RENDER_DEBUG_FEATURE_UI}; bool enabled=true;
 bool hasRenderDebugFeatureMask(int)const{return enabled;} } gPipeline;
struct LLViewerWindow {
 using view_handle_set_t=std::set<LLHandle<LLView>>;
 LLView* mRootView=nullptr; LLPopupView* mPopupView=nullptr; view_handle_set_t mMouseHoverViews;
 bool mMouseInWindow=true;
 void updateMouseHoverViews(S32,S32,MASK,bool);
};
PRODUCTION_HOVER
int main() {
 LLView root, first, second; LLPopupView popups; root.add(first); root.add(second);
 first.rect={0,0,40,40}; second.rect={50,0,90,40};
 LLViewerWindow viewer; viewer.mRootView=&root; viewer.mPopupView=&popups;
 viewer.updateMouseHoverViews(10,10,0,true); assert(first.entered==1 && first.highlight);
 viewer.updateMouseHoverViews(10,10,0,true); assert(first.entered==1 && first.left==0);
 viewer.updateMouseHoverViews(60,10,0,true); assert(!first.highlight && first.left==1 && second.entered==1);
 viewer.updateMouseHoverViews(60,10,0,false); assert(!second.highlight && viewer.mMouseHoverViews.empty());
 viewer.updateMouseHoverViews(60,10,0,true); second.rect={50,50,90,90};
 viewer.updateMouseHoverViews(60,10,0,true); assert(!second.highlight && second.left==2);
 second.rect={50,0,90,40}; viewer.updateMouseHoverViews(60,10,0,true);
 assert(second.entered==3);
 gFocusMgr.capture=&second; viewer.updateMouseHoverViews(-10,-10,0,true);
 assert(second.highlight); gFocusMgr.capture=nullptr;
 viewer.updateMouseHoverViews(-10,-10,0,true); assert(!second.highlight);
 LLView top; top.rect={0,0,40,40}; gFocusMgr.top=&top;
 viewer.updateMouseHoverViews(10,10,0,true); assert(top.highlight && !first.highlight);
 gFocusMgr.top=nullptr; viewer.updateMouseHoverViews(10,10,0,true);
 assert(!top.highlight && first.highlight);
 LLView popup; popup.rect={0,0,40,40}; popups.popups.push_back(popup.getHandle());
 viewer.updateMouseHoverViews(10,10,0,true); assert(popup.highlight);
 popups.popups.clear(); viewer.updateMouseHoverViews(10,10,0,true); assert(!popup.highlight);
 first.visible=false; viewer.updateMouseHoverViews(10,10,0,true); assert(!first.highlight);
 first.visible=true; first.opaque=true; second.rect=first.rect;
 viewer.updateMouseHoverViews(10,10,0,true); assert(first.highlight && !second.highlight);
 *first.alive=false; viewer.updateMouseHoverViews(10,10,0,false); // expired handles are skipped
 assert(viewer.mMouseHoverViews.empty());
 LLView freshRoot, button;freshRoot.add(button);button.rect={0,0,40,40};
 viewer.mRootView=&freshRoot;viewer.updateMouseHoverViews(10,10,0,true);assert(button.highlight);
 viewer.mMouseInWindow=false;viewer.updateMouseHoverViews(10,10,0,true);
 assert(!button.highlight && viewer.mMouseHoverViews.empty());
 viewer.mMouseInWindow=true;viewer.updateMouseHoverViews(10,10,0,true);assert(button.highlight);
}
