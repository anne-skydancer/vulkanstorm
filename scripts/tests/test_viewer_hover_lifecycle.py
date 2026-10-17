"""Compile the actual renderer-independent hover controller with owned UI trees."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class HoverLifecycleTest(unittest.TestCase):
    def test_diagnostics_never_mutate_the_input_desktop_pointer(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/newview/vsvkdiag.cpp').read_text()
        for forbidden in ('SetCursorPos(', 'GetCursorPos(', 'WindowFromPoint(', 'DiagnosticPointerPlacement'):
            self.assertNotIn(forbidden, source)

    def test_actual_controller(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/newview/llviewerwindow.cpp').read_text()
        start = source.index('void LLViewerWindow::updateMouseHoverViews(')
        end = source.index('// Update UI based on stored mouse position', start)
        body = source[start:end]
        native = (root / 'indra/newview/vsviewerwindow.cpp').read_text()
        begin = native.index('void LLViewerWindow::nativeHoverUpdate(')
        finish = native.index('void LLViewerWindow::nativeScroll', begin)
        native_body = native[begin:finish]
        diagnostic = (root / 'indra/newview/vsvkdiag.cpp').read_text()
        begin = diagnostic.index('        // OS input pumping also processes initial resize/layout events.')
        finish = diagnostic.index('        gFocusMgr.setMouseCapture(button);', begin)
        layout_probe = diagnostic[begin:finish]
        fixture = r'''
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
struct LLToolPipette {
 static bool instanceExists(){return false;}
 static LLToolPipette&instance(){static LLToolPipette tool;return tool;}
 bool nativeActive(){return false;}bool hasMouseCapture(){return false;}
 bool handleHover(int,int,int){return false;}bool handleToolTip(int,int,int){return false;}
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
constexpr int UI_CURSOR_ARROW=0;
struct Window { void setCursor(int) {} };
struct LLViewerWindow {
 using view_handle_set_t=std::set<LLHandle<LLView>>;
 LLView* mRootView=nullptr; LLPopupView* mPopupView=nullptr; view_handle_set_t mMouseHoverViews;
 Window* mWindow=nullptr; bool mMouseInWindow=true;
 struct Point { int mX=0,mY=0; } mCurrentMousePoint;
 void updateMouseHoverViews(S32,S32,MASK,bool);
 void nativeHoverUpdate(MASK);
 void updateUI(){nativeHoverUpdate(0);}
 Point getCurrentMouse()const{return mCurrentMousePoint;}
};
''' + body + native_body + '\nvoid require(bool value,const char*) { assert(value); }\n' + r'''
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
 // The actual native dispatcher must re-highlight a captured button outside,
 // then reconcile the retained capture handle when capture is released.
 LLView freshRoot, drag; freshRoot.add(drag); drag.rect={0,0,40,40};
 Window window; viewer.mWindow=&window; viewer.mRootView=&freshRoot;
 viewer.mCurrentMousePoint={10,10}; viewer.nativeHoverUpdate(0); assert(drag.highlight);
 gFocusMgr.capture=&drag; viewer.mCurrentMousePoint={-10,-10}; viewer.nativeHoverUpdate(0);
 assert(drag.highlight); gFocusMgr.capture=nullptr; viewer.nativeHoverUpdate(0);
 assert(!drag.highlight && drag.left==1);
 viewer.mCurrentMousePoint={10,10}; viewer.nativeHoverUpdate(0); assert(drag.highlight);
 viewer.mMouseInWindow=false; viewer.nativeHoverUpdate(0); assert(!drag.highlight);
 {
  // Resize handled while pumping OS leave relocated the authored login button.
  // The actual diagnostic must capture its current position after that pump.
  LLView layoutRoot, layoutButton; layoutRoot.rect={0,0,500,500}; layoutRoot.add(layoutButton);
  layoutButton.rect={100,200,200,240};layoutButton.entered=layoutButton.left=2;
  viewer.mRootView=&layoutRoot; viewer.mMouseInWindow=true;viewer.mCurrentMousePoint={-10,-10};
  auto* startup_window=&viewer;auto* button=&layoutButton;
  auto& enters=layoutButton.entered;auto& leaves=layoutButton.left;
  auto hoverAt=[&](int x,int y){viewer.mCurrentMousePoint={x,y};viewer.nativeHoverUpdate(0);};
  LAYOUT_PROBE
 }
}
'''.replace('LAYOUT_PROBE', layout_probe)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'hover.cpp'
            executable = Path(directory) / 'hover.exe'
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == '__main__':
    unittest.main()
