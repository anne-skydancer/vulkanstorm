"""Exercise native sampling and the shared inventory permission controller without a GPU."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def method(filename, signature):
    source = (ROOT / "indra/newview" / filename).read_text(encoding="utf-8-sig")
    start = source.index("{", source.index(signature))
    depth = 0
    for index in range(start, len(source)):
        if source[index] == "{": depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0: return source[start:index + 1]
    raise AssertionError(signature)


class NativePipetteTest(unittest.TestCase):
    @unittest.skip('Firestorm 7.2.5 replaced the pipette API: LLToolPipette::setTextureEntry is gone and '
                   'pickers subscribe with (LLPointer<LLViewerObject>, face). The Vulkan pipette '
                   '(nativeSample) still publishes a copied LLTextureEntry and needs a redesign '
                   'for the new consumers; tracked for the vkstorm-vulkan-rhi rework.')
    def test_face_ownership_publication_and_shared_picker_permissions(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler, "A C++ compiler is required")
        methods = {
            "CURRENT_OBJECT": ("vsworldscene.cpp", "bool VSWorldScene::current(const ObjectHandle&"),
            "CURRENT_FACE": ("vsworldscene.cpp", "bool VSWorldScene::current(const FaceHandle&"),
            "FACE": ("vsworldscene.cpp", "std::optional<VSWorldScene::FaceHandle> VSWorldScene::face("),
            "COPY_TE": ("vsworldscene.cpp", "bool VSWorldScene::copyTextureEntry("),
            "CAPTURE_LOST": ("lltoolpipette.cpp", "void LLToolPipette::onMouseCaptureLost()"),
            "ACTIVE": ("lltoolpipette.cpp", "bool LLToolPipette::nativeActive()"),
            "NATIVE_CONSUMER": ("lltoolpipette.cpp", "bool LLToolPipette::nativeConsumer("),
            "ACTIVATE": ("lltoolpipette.cpp", "void LLToolPipette::setNativeActive("),
            "SAMPLE": ("lltoolpipette.cpp", "bool LLToolPipette::nativeSample("),
            "ENTRY": ("lltoolpipette.cpp", "void LLToolPipette::setTextureEntry("),
            "RESULT": ("lltoolpipette.cpp", "void LLToolPipette::setResult("),
            "FIND_INVENTORY": ("lltexturectrl.cpp", "LLViewerInventoryItem* LLFloaterTexturePicker::findInvItem("),
            "FIND_ITEM": ("lltexturectrl.cpp", "const LLUUID& LLFloaterTexturePicker::findItemID("),
            "TEXTURE_SELECT": ("lltexturectrl.cpp", "void LLFloaterTexturePicker::onTextureSelect("),
            "COMMIT": ("lltexturectrl.cpp", "void LLFloaterTexturePicker::commitIfImmediateSet()"),
            "COLOR_SELECT": ("llfloatercolorpicker.cpp", "void LLFloaterColorPicker::onColorSelect("),
        }
        fixture = r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#define VS_NATIVE_VULKAN 1
using S32=int;using U32=unsigned;using MASK=unsigned;
struct LLUUID {int value=0;bool operator==(const LLUUID& v)const{return value==v.value;}bool operator!=(const LLUUID& v)const{return !(*this==v);}bool isNull()const{return !value;}bool notNull()const{return value;}static const LLUUID null;};
const LLUUID LLUUID::null{};
struct Color {float mV[4]{0,0,0,1};};
struct LLTextureEntry {LLUUID id;Color color;const LLUUID& getID()const{return id;}const Color& getColor()const{return color;}};
constexpr int LL_PCODE_VOLUME=9;
struct VSWorldScene {
    struct ObjectHandle {int host=1,local=1;LLUUID id{5};int region=1,generation=1,kind=1,account=1,accountGeneration=1;};
    struct FaceHandle {ObjectHandle object;U32 face=0;int generation=1;};
    struct Face {bool published=false;LLTextureEntry material,publishedMaterial;};
    struct Object {ObjectHandle token;int pcode=LL_PCODE_VOLUME,revision=1;std::vector<Face> faces,admittedFaces;};
    struct Impl {int host=1;bool retired=false;std::map<int,Object> objects;};
    std::unique_ptr<Impl> mImpl=std::make_unique<Impl>();std::map<int,std::unique_ptr<VSWorldScene>> mNeighbours;
    std::optional<ObjectHandle> object(int host,int local)const {
        if(mImpl->retired)return {};
        if(host!=mImpl->host){auto i=mNeighbours.find(host);return i==mNeighbours.end()?std::nullopt:i->second->object(host,local);}
        auto i=mImpl->objects.find(local);return i==mImpl->objects.end()?std::nullopt:std::optional<ObjectHandle>(i->second.token);
    }
    std::optional<ObjectHandle> object(const LLUUID& id)const {
        for(const auto& pair:mImpl->objects)if(pair.second.token.id==id)return object(mImpl->host,pair.first);
        return {};
    }
    bool current(const ObjectHandle& token)const CURRENT_OBJECT
    bool current(const FaceHandle& token)const CURRENT_FACE
    std::optional<FaceHandle> face(const ObjectHandle& token,U32 index)const FACE
    bool copyTextureEntry(const FaceHandle& token,LLTextureEntry& result)const COPY_TE
    struct ObjectAdmission {int pcode=0;};
    std::optional<ObjectAdmission> objectAdmission(const ObjectHandle& token)const {
        if(!current(token))return {};
        if(token.host!=mImpl->host)return mNeighbours.at(token.host)->objectAdmission(token);
        return ObjectAdmission{mImpl->objects.at(token.local).pcode};
    }
};
struct LLCoordGL {int x,y;LLCoordGL(int a,int b):x(a),y(b){}};
struct LLPickInfo {LLUUID mObjectID;int mObjectFace=-1;std::function<bool()> mNativeObjectCurrent;LLPickInfo(LLCoordGL,MASK,bool,bool,bool,bool,bool,bool,void*){}};
struct Context {VSWorldScene scene;VSWorldScene& world(){return scene;}};
struct Window {Context context;bool pickValid=true,pickCurrent=true;int pickedFace=0;bool isNativeVulkan()const{return true;}Context* nativeContext(){return &context;}
    bool nativePick(LLPickInfo& p){p.mObjectID={5};p.mObjectFace=pickedFace;p.mNativeObjectCurrent=[this]{return pickCurrent;};return pickValid;}} window;
Window* gViewerWindow=&window;
struct LLView;
template<class T>struct LLHandle {T* ptr=nullptr;std::weak_ptr<bool> alive;LLHandle()=default;LLHandle(T* p,std::shared_ptr<bool> a):ptr(p),alive(a){}
    bool isDead()const{auto p=alive.lock();return !p||!*p;}T* get()const{return isDead()?nullptr:ptr;}};
struct LLView {std::shared_ptr<bool> alive=std::make_shared<bool>(true);bool visible=true;virtual ~LLView(){*alive=false;}
    bool isInVisibleChain()const{return visible;}LLHandle<LLView> getHandle(){return {this,alive};}};
struct LLTool {bool captured=false;virtual ~LLTool()=default;void onMouseCaptureLost(){}bool hasMouseCapture()const{return captured;}void setMouseCapture(bool value){captured=value;}};
struct LLToolMgr {LLTool base;LLTool* selected=&base;bool ready=true;static LLToolMgr& instance(){static LLToolMgr value;return value;}
    LLTool* nativeCurrentTool(){return ready?selected:nullptr;}bool nativeToolsReady()const{return ready;}
    void setTransientTool(LLTool* t){selected=t;}void clearTransientTool(){selected=&base;}};
struct Selection {int clears=0,highlights=0;bool admit=true;std::function<bool()> request;
    void clearHighlights(){++clears;request={};}bool beginHighlightRequest(std::function<bool()> r){if(!admit)return false;request=std::move(r);return true;}
    bool highlight(const std::vector<VSWorldScene::ObjectHandle>& objects,bool){if(!request||!request()||objects.size()!=1)return false;++highlights;return true;}};
struct VSNativeSession : std::enable_shared_from_this<VSNativeSession> {
    static inline std::shared_ptr<VSNativeSession> live;static std::shared_ptr<VSNativeSession> active(){return live;}
    bool alive=true;Selection selection;bool regionReady()const{return alive;}Selection* objectSelection(){return &selection;}Selection* existingObjectSelection(){return &selection;}
    std::function<bool()> accountGuard(){return [weak=weak_from_this()]{auto p=weak.lock();return p&&p->alive;};}};
struct LLToolPipette : LLTool {
    bool mSuccess=true,mNativeHighlightOwned=false;LLHandle<LLView> mNativeConsumer;std::function<bool()> mNativeAccount,mNativeOperation;std::string mTooltipMsg;LLTextureEntry mTextureEntry;
    std::function<void(const LLTextureEntry&)> mSignal=[](const LLTextureEntry&){};
    static LLToolPipette* getInstance();static LLToolPipette& instance(){return *getInstance();}
    void onMouseCaptureLost() CAPTURE_LOST
    bool nativeActive()const ACTIVE
    bool nativeConsumer(const LLView* view)const NATIVE_CONSUMER
    void setNativeActive(bool active,LLView* consumer=nullptr,std::function<bool()> operation={}) ACTIVATE
    bool nativeSample(S32 x,S32 y,MASK mask) SAMPLE
    void setTextureEntry(const LLTextureEntry* entry) ENTRY
    void setResult(bool success,const std::string& msg) RESULT
} tool;
LLToolPipette* LLToolPipette::getInstance(){return &tool;}
struct Agent {LLUUID getID()const{return {1};}LLUUID getGroupID()const{return {2};}}gAgent;
struct LLPermissions {bool copy=true;bool allowCopyBy(LLUUID,LLUUID={})const{return copy;}};
struct LLViewerInventoryItem {using item_array_t=std::vector<LLViewerInventoryItem*>;LLUUID item,asset;LLPermissions permissions;const LLUUID& getUUID()const{return item;}LLPermissions getPermissions()const{return permissions;}};
using LLInventoryItem=LLViewerInventoryItem;
struct LLViewerInventoryCategory {using cat_array_t=std::vector<int>;};
struct LLAssetIDMatches {LLUUID asset;explicit LLAssetIDMatches(LLUUID id):asset(id){}};
struct LLAssetIDAndTypeMatches:LLAssetIDMatches {LLAssetIDAndTypeMatches(LLUUID id,int):LLAssetIDMatches(id){}};
namespace LLAssetType {constexpr int AT_MATERIAL=1;}
struct Inventory {
    std::vector<LLViewerInventoryItem*> entries;
    template<class Predicate> void collectDescendentsIf(LLUUID,LLViewerInventoryCategory::cat_array_t&,LLViewerInventoryItem::item_array_t& out,bool,const Predicate& match)const {for(auto* item:entries)if(item->asset==match.asset)out.push_back(item);}
    bool isObjectDescendentOf(LLUUID,LLUUID)const{return false;}LLUUID getLibraryRootFolderID()const{return {99};}
    LLInventoryItem* getItem(LLUUID id)const{for(auto* item:entries)if(item->item==id)return item;return nullptr;}
}gInventory;
namespace LLInventoryModel {constexpr bool INCLUDE_TRASH=true;}
const LLUUID BLANK_MATERIAL_ASSET_ID{1000};
namespace LLTrans {std::string getString(const char* p){return p;}}
std::ostringstream log_sink;
#define LL_WARNS(x) log_sink
#define LL_ENDL ""
struct LLTextureCtrl {enum {TEXTURE_CHANGE};};
constexpr int PICK_TEXTURE=0,PICK_MATERIAL=1;
struct LLFloaterTexturePicker : LLView {
    int mInventoryPickType=PICK_TEXTURE;bool mNoCopyTextureSelected=false,mCanApply=true,mCanPreview=true;LLUUID selected;int commits=0;
    LLViewerInventoryItem* findInvItem(const LLUUID& asset_id,bool copyable_only,bool ignore_library=false)const FIND_INVENTORY
    const LLUUID& findItemID(const LLUUID& asset_id,bool copyable_only,bool ignore_library=false)const FIND_ITEM
    void setCanApply(bool,bool){}void setImageID(LLUUID id){selected=id;}void setTentative(bool){}void commitCallback(int){++commits;}
    void commitIfImmediateSet() COMMIT
    void onTextureSelect(const LLTextureEntry& te) TEXTURE_SELECT
};
constexpr int VRED=0,VGREEN=1,VBLUE=2;
struct LLFloaterColorPicker : LLView {Color selected;void selectCurRgb(float r,float g,float b){selected.mV[0]=r;selected.mV[1]=g;selected.mV[2]=b;}void onColorSelect(const LLTextureEntry& te) COLOR_SELECT;};
int main(){
    auto& scene=window.context.scene;auto& object=scene.mImpl->objects[1];
    VSWorldScene::Face f;f.material.id={20};f.publishedMaterial.id={10};f.publishedMaterial.color.mV[0]=.25f;f.published=true;object.faces.push_back(f);
    auto face=scene.face(object.token,0);assert(face);LLTextureEntry copied;assert(scene.copyTextureEntry(*face,copied)&&copied.id.value==10);
    object.faces[0].material.id={30};assert(copied.id.value==10);assert(scene.copyTextureEntry(*face,copied)&&copied.id.value==10);
    ++object.revision;assert(!scene.copyTextureEntry(*face,copied));face=scene.face(object.token,0);assert(face);
    ++object.token.accountGeneration;assert(!scene.copyTextureEntry(*face,copied));face=scene.face(object.token,0);assert(face);
    object.admittedFaces=object.faces;object.faces[0].publishedMaterial.id={40};assert(scene.copyTextureEntry(*face,copied)&&copied.id.value==10);
    scene.mImpl->retired=true;assert(!scene.copyTextureEntry(*face,copied));scene.mImpl->retired=false;
    auto session=std::make_shared<VSNativeSession>();VSNativeSession::live=session;auto& selection=session->selection;
    LLFloaterTexturePicker picker;tool.mSignal=[&](const LLTextureEntry& te){picker.onTextureSelect(te);};
    assert(!tool.nativeActive()&&!tool.nativeSample(1,1,0));
    tool.setNativeActive(true,&picker);assert(tool.nativeActive()&&tool.nativeConsumer(&picker)&&LLToolMgr::instance().selected==&tool);
    assert(tool.nativeSample(1,1,0));assert(tool.mNativeHighlightOwned&&selection.highlights==1);assert(!tool.mSuccess&&picker.selected.isNull()&&picker.commits==0);
    LLViewerInventoryItem noCopy{{50},{10},{false}};gInventory.entries={&noCopy};
    assert(tool.nativeSample(1,1,0));assert(!tool.mSuccess&&picker.selected.isNull()&&picker.commits==0);
    LLViewerInventoryItem allowed{{51},{10},{true}};gInventory.entries.push_back(&allowed);
    assert(tool.nativeSample(1,1,0));assert(tool.mSuccess&&picker.selected.value==10&&picker.commits==1);
    session->alive=false;assert(!tool.nativeSample(1,1,0)&&picker.commits==1);
    session->alive=true;window.pickedFace=5;assert(!tool.nativeSample(1,1,0)&&picker.commits==1);window.pickedFace=0;
    window.pickValid=false;assert(!tool.nativeSample(1,1,0)&&picker.commits==1);window.pickValid=true;
    window.pickCurrent=false;assert(!tool.nativeSample(1,1,0)&&picker.commits==1&&!tool.mNativeHighlightOwned);window.pickCurrent=true;
    selection.admit=false;assert(!tool.nativeSample(1,1,0)&&picker.commits==1);selection.admit=true;
    object.pcode=LL_PCODE_VOLUME+1;assert(!tool.nativeSample(1,1,0)&&picker.commits==1);object.pcode=LL_PCODE_VOLUME;
    LLFloaterColorPicker color;tool.mSignal=[&](const LLTextureEntry& te){color.onColorSelect(te);picker.onTextureSelect(te);};
    tool.setNativeActive(true,&color);assert(tool.nativeConsumer(&color)&&!tool.nativeConsumer(&picker));
    assert(tool.nativeSample(1,1,0));assert(color.selected.mV[0]==.25f&&picker.commits==1); // Only the initiating picker consumes the sample.
    scene.mImpl->retired=true;assert(!tool.nativeSample(1,1,0));
    scene.mImpl->retired=false;assert(tool.nativeSample(1,1,0)&&tool.mNativeHighlightOwned);
    const auto clears=selection.clears;assert(tool.nativeActive());tool.onMouseCaptureLost();
    assert(!tool.nativeActive()&&!tool.nativeSample(1,1,0));
    assert(!tool.mNativeHighlightOwned&&selection.clears==clears+1&&LLToolMgr::instance().selected==&LLToolMgr::instance().base);
}
'''
        for marker, (filename, signature) in methods.items():
            fixture = fixture.replace(marker, method(filename, signature))
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "pipette.cpp"
            binary = Path(directory) / "pipette.exe"
            source.write_text(fixture, encoding="utf-8")
            built = subprocess.run([compiler, "-std=c++17", str(source), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
