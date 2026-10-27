"""Actual production traversal, queue/lifetime and CPU image-metadata checks."""
from pathlib import Path
import os,re,shutil,subprocess,tempfile,unittest
ROOT=Path(os.environ.get('VS_MATERIAL_TEST_ROOT',Path(__file__).resolve().parents[2]))
OVERLAY=Path(os.environ['VS_MATERIAL_PROPOSED']) if 'VS_MATERIAL_PROPOSED' in os.environ else None
def source(path):
 p=OVERLAY/path if OVERLAY else ROOT/path
 return (p if p.exists() else ROOT/path).read_text()
def body(text,name):
 start=text.index(name);start=text.index('{',start);end=start+1;depth=1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
def run(test,code):
 cc=shutil.which('clang++') or shutil.which('g++');test.assertIsNotNone(cc)
 with tempfile.TemporaryDirectory() as d:
  cpp=Path(d)/'material.cpp';exe=Path(d)/'material.exe';cpp.write_text(code)
  result=subprocess.run([cc,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True);test.assertEqual(result.returncode,0,result.stdout+result.stderr)
  result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20);test.assertEqual(result.returncode,0,result.stdout+result.stderr)
class NativeMaterialSelectionTests(unittest.TestCase):
 def test_actual_selected_face_visitor_matches_original_all_and_first_semantics(self):
  native=body(source('indra/newview/vsmaterialselection.h'),'template<class F> bool applyToTEs(')
  original=body(source('indra/newview/llselectmgr.cpp'),'bool LLObjectSelection::applyToTEs(')
  run(self,r'''
#define VS_NATIVE_VULKAN 1
#include <cassert>
#include <algorithm>
#include <memory>
#include <vector>
#include <functional>
using S32=int;using U32=unsigned;
template<class T>T llmin(T a,T b){return std::min(a,b);}
struct Row{int id,count,faces;unsigned mask;};
std::vector<Row>rows;
struct Object{Row row;int getNumTEs(){return row.count;}int getNumFaces(){return row.faces;}};
struct Token{int id;};
struct VSMaterialObject{std::shared_ptr<Object>ptr;static VSMaterialObject find(int id){for(auto&r:rows)if(r.id==id)return{std::make_shared<Object>(Object{r})};return{};}static VSMaterialObject find(Token t){return find(t.id);}explicit operator bool()const{return bool(ptr);}Object*operator->(){return ptr.get();}};
struct Selection{bool active=true;std::vector<Token>selected(){std::vector<Token>v;for(auto&r:rows)v.push_back({r.id});return v;}bool node(Token){return active;}bool contains(Token t,int face=-1){if(!active)return false;for(auto&r:rows)if(r.id==t.id)return face>=0&&face<32&&((r.mask>>face)&1);return false;}};
struct LegacyUnused{template<class F>bool applyToTEs(F*,bool){return false;}};
struct Native{bool mNative=true;std::shared_ptr<Selection>mSelection=std::make_shared<Selection>();std::shared_ptr<LegacyUnused>mLegacy;
 // VSObjectSelection::selectionGuard: false once the selection it snapshotted is retired/replaced.
 std::function<bool()>guard()const{auto selection=mSelection;return[selection]{return selection->active;};}
 template<class F>bool applyToTEs(F*func,bool firstonly=false)NATIVE_BODY};
struct Node{Object*obj;bool isTESelected(int side){return(obj->row.mask>>std::min(side,31))&1;}Object*getObject(){return obj;}};
using LLSelectNode=Node;using LLViewerObject=Object;
struct Functor{std::vector<int>calls;int accept=0;std::function<void()>after=[]{};bool apply(Object*object,int side){calls.push_back(object->row.id*100+side);after();return(side+object->row.id)%3==accept;}bool apply(VSMaterialObject object,int side){return apply(object.ptr.get(),side);}};
struct Reference{using iterator=std::vector<Node*>::iterator;std::vector<Node*>list;iterator begin(){return list.begin();}iterator end(){return list.end();}bool applyToTEs(Functor*func,bool firstonly)ORIGINAL_BODY};
int main(){
 for(unsigned mask:{0u,1u,3u,0x80000000u,0xffffffffu})for(int count:{0,1,4,32})for(int faces:{0,1,3,32})for(bool first:{false,true})for(int accept:{0,1,2}){
  rows={{1,count,faces,mask},{2,3,3,3}};Object a{rows[0]},b{rows[1]};Node na{&a},nb{&b};Reference ref;ref.list={&na,&nb};Native native;Functor x,y;x.accept=y.accept=accept;
  bool expected=ref.applyToTEs(&x,first);bool actual=native.applyToTEs(&y,first);assert(expected==actual&&x.calls==y.calls);
 }
 rows={{1,3,3,7},{2,3,3,7}};Native native;Functor f;f.after=[&]{native.mSelection->active=false;};native.applyToTEs(&f);assert(f.calls.size()==1); // real reentrant selection retirement
}
'''.replace('NATIVE_BODY',native).replace('ORIGINAL_BODY',original))
 def test_actual_cpu_imported_image_dimensions_do_not_call_gl(self):
  cpp=source('indra/llrender/llgltexture.cpp')
  methods='\n'.join(sig+body(cpp,key)for sig,key in (
   ('void setCPUImageExtent(U32 width,U32 height,U8 components)','void LLGLTexture::setCPUImageExtent('),
   ('S32 getWidth(S32 discard_level=-1)const','S32 LLGLTexture::getWidth('),
   ('S32 getHeight(S32 discard_level=-1)const','S32 LLGLTexture::getHeight('),
   ('S32 getDiscardLevel()const','S32 LLGLTexture::getDiscardLevel('),
   ('S32 getMaxDiscardLevel()const','S32 LLGLTexture::getMaxDiscardLevel('),
   ('S8 getComponents()const','S8  LLGLTexture::getComponents(')))
  run(self,r'''
#include <cassert>
#include <algorithm>
#define llassert assert
using U32=unsigned;using U8=unsigned char;using S32=int;using S8=signed char;constexpr int MAX_DISCARD_LEVEL=5;
template<class T>T llmax(T a,T b){return std::max(a,b);}
struct GL{int calls=0;int getWidth(int){++calls;return 7;}int getHeight(int){++calls;return 9;}int getDiscardLevel(){++calls;return 2;}int getMaxDiscardLevel(){++calls;return 3;}S8 getComponents(){++calls;return 4;}};
struct Pointer{GL*p=nullptr;bool isNull()const{return!p;}bool notNull()const{return p;}GL*operator->()const{return p;}};
struct Texture{S8 mCPUImageDiscard=-1;bool mCPUImage=false;unsigned mFullWidth=0,mFullHeight=0;S8 mComponents=0;Pointer mGLTexturep;void setTexelsPerImage(){}METHODS};
int main(){Texture image;image.setCPUImageExtent(0,0,0);assert(image.getDiscardLevel()==-1&&image.getMaxDiscardLevel()==0);image.setCPUImageExtent(64,32,3);assert(image.getWidth()==64&&image.getHeight()==32&&image.getComponents()==3&&image.getDiscardLevel()==0&&image.getMaxDiscardLevel()==5);assert(image.getWidth(2)==16&&image.getHeight(2)==8);for(unsigned w:{1u,2u,7u,64u,1024u})for(unsigned h:{1u,3u,32u,512u}){Texture candidate;candidate.setCPUImageExtent(w,h,4);unsigned a=w,b=h;int expected=0;while(a>1&&b>1&&expected<MAX_DISCARD_LEVEL){++expected;a>>=1;b>>=1;}assert(candidate.getMaxDiscardLevel()==expected);}GL gl;Texture original;original.mGLTexturep.p=&gl;assert(original.getWidth()==7&&original.getHeight()==9&&original.getDiscardLevel()==2&&original.getMaxDiscardLevel()==3&&original.getComponents()==4&&gl.calls==5);}
'''.replace('METHODS',methods))
 def test_actual_native_queue_copies_values_and_callback_retires(self):
  cpp=source('indra/newview/llgltfmateriallist.cpp');header=source('indra/newview/llgltfmateriallist.h')
  queue=body(cpp,'void LLGLTFMaterialList::queueModify(const LLUUID& id')
  holder='class CallbackHolder'+body(header,'    class CallbackHolder')+';'
  run(self,r'''
#include <cassert>
#include <functional>
#include <list>
#include <memory>
#include <string>
using LLUUID=int;using S32=int;
struct LLGLTFMaterial{int value=0;};
struct Queue{struct ModifyMaterialData{int object_id;int side;LLGLTFMaterial override_data;bool has_override=false;std::function<bool()>current;std::string capability;};std::list<ModifyMaterialData>sModifyQueue;void queueModify(const LLUUID&id,S32 side,const LLGLTFMaterial*material,std::function<bool()>current,const std::string&capability)QUEUE};
HOLDER
int main(){Queue q;bool active=true;LLGLTFMaterial material{19};auto current=[&]{return active;};q.queueModify(7,4,&material,current,"cap-A");material.value=99;assert(q.sModifyQueue.size()==1&&q.sModifyQueue.front().override_data.value==19&&q.sModifyQueue.front().capability=="cap-A");q.queueModify(7,4,&material,current,"");assert(q.sModifyQueue.size()==1);active=false;q.queueModify(7,4,&material,current,"cap-B");assert(q.sModifyQueue.size()==1&&!q.sModifyQueue.front().current());int callbacks=0;{CallbackHolder c([&](bool){++callbacks;},current);}assert(callbacks==0);active=true;{CallbackHolder c([&](bool success){assert(success);++callbacks;},current);}assert(callbacks==1);{auto c=std::make_shared<CallbackHolder>([&](bool){++callbacks;},current);active=false;c.reset();}assert(callbacks==1);}
'''.replace('QUEUE',queue).replace('HOLDER',holder))
 def test_original_editor_value_and_revert_controller_body_is_preserved(self):
  original=source('scripts/tests/material_override_reference.cpp')
  native=source('indra/newview/llmaterialeditor.cpp')
  native=native[native.index('class LLRenderMaterialOverrideFunctor'):]
  native=body(native,'bool apply(VSMaterialNode* nodep)')
  expected=body(original,'virtual bool apply(LLSelectNode* nodep)')
  expected=expected.replace('LLViewerObject* objectp = nodep->getObject();','auto objectp = nodep->getObject();').replace('LLGLTFMaterialList::queueModify(objectp, te, material);','objectp->queueModify(te, material);')
  self.assertEqual(native,expected)
 def test_actual_local_image_completion_pause_failure_and_reentrant_retirement(self):
  cpp=source('indra/newview/llviewertexture.cpp')
  native=body(body(cpp,'bool LLViewerFetchedTexture::doLoadedCallbacks()'),'if (mNativeLocalImageCurrent)')
  retire=body(cpp,'void LLViewerFetchedTexture::retireCPULocalImage()')
  run(self,r'''
#include <cassert>
#include <functional>
#include <list>
#include <memory>
#include <set>
#include <cstdint>
constexpr int S8_MAX=127;
template<class T>struct LLPointer{T*p=nullptr;LLPointer()=default;LLPointer(T*v):p(v){}bool notNull()const{return p;}T*get()const{return p;}};
struct Raw{int pixel=19;};
struct LLViewerFetchedTexture;
struct LLLoadedCallbackEntry{bool mPaused=false,mNeedsImageRaw=true;void*mUserData=nullptr;std::set<int>*source=nullptr;std::function<void(bool,LLViewerFetchedTexture*,Raw*,Raw*,int,bool,void*)>mCallback;void removeTexture(LLViewerFetchedTexture*){if(source)source->erase(7);}};
int retirements=0;void vs_retire_local_image(int){++retirements;}
struct LLViewerFetchedTexture{
 unsigned long long mNativeLocalDecodeRevision=0;void invalidateCPUImage(){}
 bool mNativeLocalPending=false,mPauseLoadedCallBacks=false,mNativeLocalRetired=false,mIsMissingAsset=false;
 std::function<bool()>mNativeLocalImageCurrent=[] {return true;};std::list<LLLoadedCallbackEntry*>mLoadedCallbackList;LLPointer<Raw>mSavedRawImage;int mLoadedCallbackDesiredDiscardLevel=0;
 int getID(){return 7;}bool doLoadedCallbacks()NATIVE_BODY
 void retireCPULocalImage()RETIRE_BODY
};
int main(){Raw pixels;LLViewerFetchedTexture image;image.mSavedRawImage=&pixels;std::set<int>source{7};int success=0,failure=0;
 auto add=[&](bool pause=false){auto*entry=new LLLoadedCallbackEntry;entry->source=&source;entry->mPaused=pause;entry->mCallback=[&](bool ok,auto*self,auto*raw,auto*,int level,bool final,void*){assert(self==&image&&level==0&&final);if(ok){assert(raw==&pixels);++success;}else{assert(!raw);++failure;}};image.mLoadedCallbackList.push_back(entry);return entry;};
 image.mNativeLocalPending=true;add();assert(!image.doLoadedCallbacks()&&success==0);image.mNativeLocalPending=false;assert(image.doLoadedCallbacks()&&success==1&&source.empty());assert(!image.doLoadedCallbacks()&&success==1);
 source.insert(7);auto*paused=add(true);assert(!image.doLoadedCallbacks()&&image.mLoadedCallbackList.size()==1&&source.size()==1);paused->mPaused=false;image.doLoadedCallbacks();assert(success==2&&source.empty());
 source.insert(7);add();image.mNativeLocalImageCurrent=[] {return false;};image.doLoadedCallbacks();assert(failure==1&&source.empty());
 image.mNativeLocalImageCurrent=[] {return true;};source.insert(7);auto*first=add();first->mCallback=[&](bool ok,auto*,auto*,auto*,int,bool,void*){assert(ok);++success;image.retireCPULocalImage();};add();image.doLoadedCallbacks();assert(success==3&&failure==2&&image.mLoadedCallbackList.empty()&&source.empty()&&retirements==1);
}
'''.replace('NATIVE_BODY',native).replace('RETIRE_BODY',retire))
 def test_actual_flush_batches_original_payload_by_cap_and_rejects_retired_request(self):
  cpp=source('indra/newview/llgltfmateriallist.cpp');header=source('indra/newview/llgltfmateriallist.h')
  holder='class CallbackHolder'+body(header,'    class CallbackHolder')+';'
  entry='struct ModifyMaterialData'+body(header,'    struct ModifyMaterialData')+';'
  apply_entry='struct ApplyMaterialAssetData'+body(header,'    struct ApplyMaterialAssetData')+';'
  methods='\n'.join(sig+body(cpp,key)for sig,key in (
   ('static void queueModify(const LLUUID&id,S32 side,const LLGLTFMaterial*material,std::function<bool()>current,const std::string&capability)','void LLGLTFMaterialList::queueModify(const LLUUID& id'),
   ('static void queueApply(const LLUUID&id,S32 side,const LLUUID&asset,const LLGLTFMaterial*material,std::function<bool()>current,const std::string&capability)','void LLGLTFMaterialList::queueApply(const LLUUID& id'),
   ('static void flushUpdates(std::function<void(bool)>done_callback,std::function<bool()>current)','void LLGLTFMaterialList::flushUpdates(std::function<void(bool)>'),
   ('static void flushUpdatesOnce(std::shared_ptr<CallbackHolder>callback_holder)','void LLGLTFMaterialList::flushUpdatesOnce(')))
  run(self,r'''
#include <cassert>
#include <map>
#include <list>
#include <vector>
#include <string>
#include <functional>
#include <memory>
using LLUUID=int;using S32=int;
#define llassert assert
const size_t MAX_TASK_UPDATES=255;
struct LLSD{int integer=0;std::string text;std::map<std::string,LLSD>map;std::vector<LLSD>array;static LLSD emptyArray(){return{};}size_t size()const{return array.size();}LLSD&operator[](size_t n){if(array.size()<=n)array.resize(n+1);return array[n];}LLSD&operator[](const char*s){return map[s];}LLSD&operator=(int n){integer=n;return*this;}LLSD&operator=(const std::string&s){text=s;return*this;}LLSD&operator=(const char*s){text=s;return*this;}};
bool is_valid_update(const LLSD&){return true;}
struct LLGLTFMaterial{int value=0;std::string asJSON()const{return std::to_string(value);}};
template<class T>struct LLPointer:std::shared_ptr<T>{using std::shared_ptr<T>::shared_ptr;LLPointer(T*p):std::shared_ptr<T>(p){}};
struct Agent{std::string getRegionCapability(const char*){return"GL-current-cap";}}gAgent;
struct LLCoros{std::vector<std::function<void()>>tasks;static LLCoros&instance(){static LLCoros c;return c;}template<class F>void launch(const char*,F f){tasks.emplace_back(std::move(f));}void run(){auto work=std::move(tasks);tasks.clear();for(auto&f:work)f();}};
#define LLGLTFMaterialList Service
struct Service{
HOLDER
ENTRY
APPLY_ENTRY
inline static std::list<ModifyMaterialData>sModifyQueue;inline static std::list<ApplyMaterialAssetData>sApplyQueue;inline static LLSD sUpdates;
struct Request{std::string cap;LLSD data;std::shared_ptr<CallbackHolder>holder;};inline static std::vector<Request>posted;
static void modifyMaterialCoro(std::string cap,LLSD data,std::shared_ptr<CallbackHolder>holder){if(holder&&holder->mCurrent&&!holder->mCurrent())return;posted.push_back({cap,std::move(data),std::move(holder)});}
METHODS
};
int main(){bool a=true,b=true;int finished=0;LLGLTFMaterial material{17};
 for(int side=0;side<256;++side)Service::queueModify(7,side,&material,[&]{return a;},"region-A");
 Service::queueModify(9,4,nullptr,[&]{return b;},"region-B");
 Service::flushUpdates([&](bool ok){assert(ok);++finished;},[]{return true;});assert(Service::sModifyQueue.empty()&&finished==0);LLCoros::instance().run();assert(Service::posted.size()==3&&Service::posted[0].cap=="region-A"&&Service::posted[0].data.size()==255&&Service::posted[1].data.size()==1&&Service::posted[2].cap=="region-B");
 auto&row=Service::posted[0].data[size_t(0)];assert(row["object_id"].integer==7&&row["side"].integer==0&&row["gltf_json"].text=="17");assert(Service::posted[2].data[size_t(0)]["gltf_json"].text.empty());Service::posted.clear();assert(finished==1);
 Service::queueModify(7,1,&material,[&]{return a;},"region-A");Service::flushUpdates({},{});a=false;LLCoros::instance().run();assert(Service::posted.empty()); // actual launch-to-dispatch object retirement
 b=false;Service::queueModify(9,1,&material,[&]{return b;},"region-B");Service::flushUpdates({},{});assert(Service::sModifyQueue.empty()&&LLCoros::instance().tasks.empty());
 a=b=true;Service::queueModify(7,1,&material,[&]{return a;},"region-A");Service::queueApply(9,3,77,&material,[&]{return b;},"region-B");Service::flushUpdates({},{});LLCoros::instance().run();assert(Service::posted.size()==2&&Service::posted[0].cap=="region-A"&&Service::posted[1].cap=="region-B");auto&applied=Service::posted[1].data[size_t(0)];assert(applied["asset_id"].integer==77&&applied["gltf_json"].text=="17");Service::posted.clear();
 Service::queueApply(9,3,77,&material,[&]{return b;},"region-B");Service::flushUpdates({},{});b=false;LLCoros::instance().run();assert(Service::posted.empty());Service::queueApply(9,3,77,&material,[&]{return b;},"region-B");assert(Service::sApplyQueue.empty());
 b=true;Service::sModifyQueue.push_back({30,2,material,true});Service::queueApply(9,3,77,nullptr,[&]{return b;},"region-B");Service::flushUpdates({},{});LLCoros::instance().run();assert(Service::posted.size()==2&&Service::posted[0].cap=="GL-current-cap"&&Service::posted[1].cap=="region-B");Service::posted.clear();
 Service::queueModify(7,1,&material,[&]{return a;},"region-A");Service::sApplyQueue.push_back({30,2,10,nullptr});Service::flushUpdates({},{});LLCoros::instance().run();assert(Service::posted.size()==2&&Service::posted[0].cap=="region-A"&&Service::posted[1].cap=="GL-current-cap");Service::posted.clear();
}
'''.replace('HOLDER',holder).replace('APPLY_ENTRY',apply_entry).replace('ENTRY',entry).replace('METHODS',methods))
 def test_actual_same_id_reload_cancels_old_waiters_and_decode_intents(self):
  cpp=source('indra/newview/llviewertexture.cpp')
  clear=body(body(cpp,'void LLViewerFetchedTexture::clearFetchedResults()'),'if (mNativeLocalImageCurrent)')
  completion=body(cpp,'}, [image, current, id, revision](LLPointer<LLImageRaw> raw)')
  run(self,r'''
#define VS_NATIVE_VULKAN 1
#include <cassert>
#include <functional>
#include <list>
#include <memory>
#include <string>
using U64=unsigned long long;
struct Texture;struct LLLoadedCallbackEntry{std::function<void(bool,Texture*,void*,void*,int,bool,void*)>mCallback;void*mUserData=nullptr;void removeTexture(Texture*){}};
template<class T>struct LLPointer{T*p;LLPointer(T*v):p(v){}T*get()const{return p;}};
struct LLImageRaw{};
struct Texture{U64 mNativeLocalDecodeRevision=0;bool mNativeLocalPending=false,mIsMissingAsset=false,mFullyLoaded=true,mIsFetched=true,mNativeLocalRetired=false;int*mSavedRawImage=nullptr;int mSavedRawDiscardLevel=0,discard=0,starts=0,completed=0;std::string mUrl="file://asset.png";std::function<bool()>mNativeLocalImageCurrent=[] {return true;};std::list<LLLoadedCallbackEntry*>mLoadedCallbackList;void invalidateCPUImage(){discard=-1;}void startCPUFileDecode(const std::string&){++starts;++mNativeLocalDecodeRevision;mNativeLocalPending=true;}void completeCPULocalImage(LLImageRaw*){++completed;}void clearFetchedResults()CLEAR};
using LLViewerFetchedTexture=Texture;
Texture*cached=nullptr;struct LLViewerTextureManager{static Texture*findCPULocalTexture(int){return cached;}};
void complete(LLPointer<Texture>image,std::function<bool()>current,int id,U64 revision,LLPointer<LLImageRaw>raw)COMPLETE
int main(){Texture image;cached=&image;int failures=0;auto*e=new LLLoadedCallbackEntry;e->mCallback=[&](bool success,auto*,auto*,auto*,int,bool final,void*){assert(!success&&final);++failures;};image.mLoadedCallbackList.push_back(e);U64 old=image.mNativeLocalDecodeRevision;image.clearFetchedResults();assert(failures==1&&image.starts==1&&image.discard==-1&&image.mNativeLocalPending);complete(&image,[]{return true;},7,old,nullptr);assert(image.completed==0);complete(&image,[]{return true;},7,image.mNativeLocalDecodeRevision,nullptr);assert(image.completed==1);image.mNativeLocalRetired=true;complete(&image,[]{return true;},7,image.mNativeLocalDecodeRevision,nullptr);assert(image.completed==1);Texture pending;pending.mNativeLocalPending=true;pending.clearFetchedResults();assert(pending.starts==0&&pending.mNativeLocalDecodeRevision==0);Texture reentrant;auto*r=new LLLoadedCallbackEntry;r->mCallback=[&](auto...){reentrant.mNativeLocalRetired=true;};reentrant.mLoadedCallbackList.push_back(r);reentrant.clearFetchedResults();assert(reentrant.starts==0);}
'''.replace('CLEAR',clear.replace('LLPointer<LLViewerFetchedTexture>','LLPointer<Texture>')).replace('COMPLETE',completion))

if __name__=='__main__':unittest.main()
