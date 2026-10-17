"""Exercise the actual per-bake dependency key independently of body geometry."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT=Path(__file__).resolve().parents[2]


class AvatarBakeSignatureTest(unittest.TestCase):
    def test_appearance_layers_order_and_asset_generations(self):
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source=(ROOT/'indra/newview/vsworldavatar.cpp').read_text()
        production=method(source,'std::vector<U64> bakeSignature(')
        fixture=r'''
#include <algorithm>
#include <bit>
#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>
using U64=unsigned long long;using U32=unsigned;using S32=int;constexpr unsigned UUID_BYTES=16;
struct UUID {unsigned char mData[16]{};UUID(unsigned v=0){mData[0]=v;}
 bool notNull()const{return mData[0]!=0;}std::string asString()const{return std::to_string(mData[0]);}
 bool operator!=(UUID b)const{return !std::equal(mData,mData+16,b.mData);}};
const UUID IMG_DEFAULT_AVATAR{90},IMG_INVISIBLE{91};
std::map<int,UUID> nativeAliases;
UUID vs_native_image_id(const UUID& id){auto found=nativeAliases.find(id.mData[0]);return found==nativeAliases.end()?id:found->second;}

struct Param {int id;float weight;Param(int i,float w):id(i),weight(w){}virtual ~Param()=default;
 int getID()const{return id;}float getWeight()const{return weight;}};
struct LLTexLayerParam:Param {int type;LLTexLayerParam(int id,float weight,int t):Param(id,weight),type(t){}
 int getWearableType()const{return type;}};
struct Texture {UUID id;const UUID& getID()const{return id;}};
struct Wearable {int type;std::map<int,Param*> mVisualParamIndexMap;std::map<int,Texture> textures;
 int getType()const{return type;}const Texture* getLocalTextureObject(int i)const{
 auto found=textures.find(i);return found==textures.end()?nullptr:&found->second;}};
struct LLAvatarAppearanceDictionary {struct BakedEntry {std::vector<int> mWearables,mLocalTextures;};};
namespace LLUI {int requests=0;void getUIImageByID(UUID){++requests;}}
struct Resources {std::map<std::string,U64> images;
 bool image(const std::string& key)const{return images.count(key)!=0;}
 U64 generation(const std::string& key)const{return images.at(key);}};
struct Avatar {int sex=1;std::vector<Param*> params;unsigned cursor=0;
 std::vector<std::shared_ptr<Wearable>> ownedWearables;Resources resources;
 int getSex()const{return sex;}Param* getFirstVisualParam(){cursor=0;return params.empty()?nullptr:params[0];}
 Param* getNextVisualParam(){return ++cursor<params.size()?params[cursor]:nullptr;}
PRODUCTION
};
int main(){Avatar avatar;Param height{33,.25f};LLTexLayerParam skinColor{100,.4f,1};
 LLTexLayerParam shirtColor{200,.6f,4},otherShirtColor{200,.2f,4};avatar.params={&height,&skinColor};
 auto skin=std::make_shared<Wearable>();skin->type=1;skin->textures={{0,{UUID{1}}},{1,{UUID{2}}}};
 auto shirt=std::make_shared<Wearable>();shirt->type=4;shirt->mVisualParamIndexMap={{200,&shirtColor}};
 auto other=std::make_shared<Wearable>(*shirt);other->mVisualParamIndexMap={{200,&otherShirtColor}};
 avatar.ownedWearables={skin,shirt};avatar.resources.images={{"remote:1",1},{"remote:2",1}};
 LLAvatarAppearanceDictionary::BakedEntry head{{0,1},{0}},upper{{0,1,4},{1}};
 bool ready=true;auto h=avatar.bakeSignature(head,ready),u=avatar.bakeSignature(upper,ready);assert(ready);
 height.weight=.85f;assert(avatar.bakeSignature(head,ready)==h&&avatar.bakeSignature(upper,ready)==u);
 shirtColor.weight=.8f;assert(avatar.bakeSignature(head,ready)==h&&avatar.bakeSignature(upper,ready)!=u);
 shirtColor.weight=.6f;skinColor.weight=.3f;
 assert(avatar.bakeSignature(head,ready)!=h&&avatar.bakeSignature(upper,ready)!=u);skinColor.weight=.4f;
 avatar.ownedWearables.push_back(other);auto order=avatar.bakeSignature(upper,ready);
 std::swap(avatar.ownedWearables[1],avatar.ownedWearables[2]);assert(avatar.bakeSignature(upper,ready)!=order);
 assert(avatar.bakeSignature(head,ready)==h);
 avatar.resources.images["remote:1"]=2;assert(avatar.bakeSignature(head,ready)!=h);
 nativeAliases[1]=UUID{3};avatar.resources.images["remote:3"]=7;
 ready=true;auto aliased=avatar.bakeSignature(head,ready);assert(ready&&aliased!=h);
 avatar.resources.images["remote:3"]=8;assert(avatar.bakeSignature(head,ready)!=aliased);
 nativeAliases.clear();
 avatar.resources.images.erase("remote:1");ready=true;avatar.bakeSignature(head,ready);assert(!ready);
 assert(LLUI::requests>0);
}
'''.replace('PRODUCTION',production)
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'avatar_bake_signature.cpp';exe=Path(directory)/'avatar_bake_signature.exe'
            cpp.write_text(fixture)
            built=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)


if __name__=='__main__':unittest.main()
