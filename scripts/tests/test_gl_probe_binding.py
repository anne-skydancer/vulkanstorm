"""Execute actual probe binding/retirement code with strict texture target tracking."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]


class ProbeBindingTest(unittest.TestCase):
    def test_probe_array_target_and_texture_zero_retirement(self):
        source=(ROOT/'indra/llrender/llcubemaparray.cpp').read_text()
        array=source[source.index('void LLCubeMapArray::bind('):source.index('GLuint LLCubeMapArray::getGLName(')]
        source=(ROOT/'indra/newview/pipeline.cpp').read_text()
        retire=source[source.index('void LLPipeline::unbindReflectionProbes('):source.index('inline float sgn(')]
        fixture=r'''
#include <cassert>
#include <stdexcept>
using S32=int;
struct LLTexUnit {
 enum eTextureType {TT_TEXTURE,TT_RECT_TEXTURE,TT_CUBE_MAP,TT_CUBE_MAP_ARRAY,TT_NONE};
 eTextureType current=TT_NONE;
 void bindManual(eTextureType type,int,bool){current=type;}
 void unbind(eTextureType type){assert(type==TT_CUBE_MAP_ARRAY);current=TT_NONE;}
 void enable(eTextureType type){current=type;}
};
struct Render {LLTexUnit units[16];LLTexUnit* getTexUnit(int slot){assert(slot>=0&&slot<16);return &units[slot];}} gGL;
struct Image {bool getUseMipMaps(){return true;}} image;
struct LLCubeMapArray {
 int mTextureStage=-1;Image* mImage=&image;
 int getGLName(){return 1;}void bind(S32);void unbind();
};
struct LLShaderMgr {enum {REFLECTION_PROBES=7};};
struct LLGLSLShader {
 int channel=-1;
 S32 disableTexture(int uniform,LLTexUnit::eTextureType type){
  assert(uniform==LLShaderMgr::REFLECTION_PROBES);
  if(channel<0)return channel;
  auto* unit=gGL.getTexUnit(channel);
  if(unit->current!=type)throw std::runtime_error("Wrong probe sampler target");
  unit->current=LLTexUnit::TT_NONE;return channel;
 }
};
struct Owner {LLCubeMapArray* p=nullptr;bool notNull(){return p!=nullptr;}LLCubeMapArray* operator->(){return p;}};
struct LLPipeline {
 struct Manager {Owner mTexture;} mReflectionMapManager;
 void unbindReflectionProbes(LLGLSLShader&);
};
''' + array + retire + r'''
int main(){
 LLCubeMapArray map;LLPipeline pipeline;pipeline.mReflectionMapManager.mTexture.p=&map;
 LLGLSLShader shader;
 for(int channel:{8,0}){
  map.bind(channel);shader.channel=channel;
  pipeline.unbindReflectionProbes(shader);
  assert(map.mTextureStage==-1);
  assert(gGL.units[channel].current==(channel==0?LLTexUnit::TT_TEXTURE:LLTexUnit::TT_NONE));
 }
 shader.channel=-1;pipeline.mReflectionMapManager.mTexture.p=nullptr;
 pipeline.unbindReflectionProbes(shader);
}
'''
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler,'Actual render lifecycle fixture requires a C++ compiler')
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'probe.cpp';path.write_text(fixture)
            executable=Path(directory)/'probe-test.exe'
            built=subprocess.run([compiler,'-std=c++17',str(path),'-o',str(executable)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            result=subprocess.run([str(executable)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)


if __name__=='__main__':unittest.main()
