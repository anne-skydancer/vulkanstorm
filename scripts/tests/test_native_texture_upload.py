"""Execute native CPU upload preparation with an independent BC3 decoder."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class NativeTextureUploadTests(unittest.TestCase):
    def test_codec_pin_and_license(self):
        directory = ROOT / 'indra/newview/third_party/stb'
        lock = json.loads((directory / 'lock.json').read_text())
        self.assertEqual(hashlib.sha256((directory / lock['file']).read_bytes()).hexdigest(), lock['sha256'])
        self.assertEqual(lock['revision'], '7023e273f1513b68b8d4086077c6faca555d50df')
        notice = (ROOT / 'indra/newview/app_settings/stb_dxt-license.txt').read_text()
        self.assertIn('Permission is hereby granted', notice)
        self.assertIn(lock['revision'], notice)

    def test_roles_mips_edge_blocks_alpha_and_malformed_publication(self):
        source = r'''
#define STB_DXT_IMPLEMENTATION
#include "vstextureupload.h"
#include <cassert>
#include <cmath>
using namespace VSTextureUpload;
using VSWorldContracts::ImageRole;
std::array<int,4> decode(const std::vector<std::uint8_t>& block,unsigned pixel) {
 auto color=[](unsigned value) {return std::array<int,3>{int((value>>11)*255/31),int(((value>>5)&63)*255/63),int((value&31)*255/31)};};
 const unsigned a=block[8]|(unsigned(block[9])<<8),b=block[10]|(unsigned(block[11])<<8);
 auto c0=color(a),c1=color(b);std::array<std::array<int,3>,4> colors{c0,c1};
 for(unsigned k=0;k<3;++k) {colors[2][k]=(2*c0[k]+c1[k])/3;colors[3][k]=(c0[k]+2*c1[k])/3;}
 unsigned bits=0;for(unsigned k=0;k<4;++k) bits|=unsigned(block[12+k])<<(k*8);
 std::array<int,8> alpha{block[0],block[1]};
 if(alpha[0]>alpha[1]) for(int k=1;k<=6;++k) alpha[k+1]=((7-k)*alpha[0]+k*alpha[1])/7;
 else {for(int k=1;k<=4;++k) alpha[k+1]=((5-k)*alpha[0]+k*alpha[1])/5;alpha[6]=0;alpha[7]=255;}
 std::uint64_t abits=0;for(unsigned k=0;k<6;++k) abits|=std::uint64_t(block[2+k])<<(k*8);
 auto c=colors[(bits>>(pixel*2))&3];return {c[0],c[1],c[2],alpha[(abits>>(pixel*3))&7]};
}
int main() {
 std::vector<std::uint8_t> pixels(8*8*4);for(unsigned p=0;p<64;++p) {pixels[p*4]=73;pixels[p*4+1]=121;pixels[p*4+2]=199;pixels[p*4+3]=173;}
 auto image=prepare(8,8,pixels,ImageRole::RemoteAsset,true);
 assert(valid(*image) && image->compressed && image->compressionRequested && image->gpuBytes==112 && image->pixels==pixels);
 for(unsigned p=0;p<16;++p) {auto value=decode(image->levels[0].bytes,p);for(unsigned k=0;k<4;++k) assert(std::abs(value[k]-pixels[k])<=8);}
 for(auto role:{ImageRole::CoreUI,ImageRole::AvatarComposite,ImageRole::Media}) {
   auto exact=prepare(8,8,pixels,role,true);assert(valid(*exact) && !exact->compressed && exact->levels[0].bytes==pixels);
 }
 auto tiny=prepare(1,1,{1,2,3,4},ImageRole::RemoteAsset,true);assert(valid(*tiny) && !tiny->compressed && tiny->compressionRequested);
 std::vector<std::uint8_t> odd(5*7*4,255);auto edge=bc3(5,7,odd);assert(edge.size()==64);
 for(unsigned offset=0;offset<64;offset+=16) {std::vector<std::uint8_t> block(edge.begin()+offset,edge.begin()+offset+16);for(unsigned p=0;p<16;++p) {auto value=decode(block,p);for(auto c:value) assert(c==255);}}
 auto broken=*image;broken.levels.pop_back();assert(!valid(broken));
 broken=*image;broken.levels[0].rowBytes++;assert(!valid(broken));
 broken=*image;broken.gpuBytes++;assert(!valid(broken));
 broken=*image;broken.role=ImageRole::Media;assert(!valid(broken));
 broken=*image;broken.pixels.pop_back();assert(!valid(broken));
 bool refused=false;try {bc3(0,7,odd);}catch(const std::invalid_argument&) {refused=true;}assert(refused);
}
'''
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            cpp = directory / 'texture_upload.cpp'
            cpp.write_text(source)
            result = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'indra/newview'), '-I', str(ROOT / 'indra/llrender'), str(cpp), '-o', str(directory / 'upload.exe')],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(directory / 'upload.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
