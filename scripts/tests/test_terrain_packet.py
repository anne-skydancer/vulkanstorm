"""Exercise the production bounded terrain decoder without a window or GPU."""
from pathlib import Path
import test_world_scene_contracts
import unittest

ROOT = Path(__file__).resolve().parents[2]

class TerrainPacket(unittest.TestCase):
    def test_wire_coefficients_bounds_and_atomic_rejection(self):
        idct = (ROOT / 'indra/llmessage/patch_idct.cpp').read_text(encoding='utf-8')
        idct = '\n'.join(line for line in idct.splitlines() if not line.startswith('#include'))
        dct_header = (ROOT / 'indra/llmessage/patch_dct.h').as_posix()
        header = (ROOT / 'indra/newview/vsterrainpacket.h').as_posix()
        test_world_scene_contracts.WorldSceneContracts().compile('#include "' + header + '"\n' + r'''
#include <algorithm>
#include <cassert>
using F32=float;using S32=int;using U8=uint8_t;using S8=int8_t;using U16=uint16_t;using U32=uint32_t;
constexpr float F_PI=3.14159265358979323846f,OO_SQRT2=.7071067811865475244f;
constexpr unsigned VZ=2;
struct LLVector3 {float mV[3];};
''' + '#include "' + dct_header + '"\n' + idct + r'''
struct Writer {
 std::vector<uint8_t> data;unsigned cursor=0;
 void put(uint32_t value,unsigned count) {
  for(unsigned shift=0;count;shift+=8) {
   const unsigned chunk=std::min(count,8u);const unsigned byte=(value>>shift)&((1u<<chunk)-1);
   for(unsigned i=0;i<chunk;++i,++cursor) {
    if(cursor%8==0)data.push_back(0);
    data.back()|=((byte>>(chunk-i-1))&1)<<(7-cursor%8);
   }count-=chunk;
  }
 }
};
std::vector<uint8_t> packet(unsigned size,unsigned type,unsigned id) {
 Writer w;w.put(264,16);w.put(size,8);w.put(type,8);w.put(0,8);
 float offset=10;uint32_t raw;std::memcpy(&raw,&offset,4);w.put(raw,32);w.put(20,16);w.put(id,type=='M'?32:10);
 w.put(1,1);w.put(1,1);w.put(1,1);w.put(3,2); // DC coefficient -3
 w.put(1,1);w.put(0,1); // End of coefficients
 w.put(97,8);return w.data;
}
int main() {
 std::vector<VSTerrain::Patch> result;
 auto bytes=packet(16,'L',3*32+5);assert(VSTerrain::decode(bytes,'L',result));
 assert(result.size()==1&&result[0].x==3&&result[0].y==5&&result[0].offset==10);
 assert(result[0].range==20&&result[0].coefficients[0]==-3&&result[0].coefficients[1]==0);
 LLGroupHeader group;group.stride=16;group.patch_size=16;group.layer_type='L';
 LLPatchHeader header;header.quant_wbits=0;header.dc_offset=10;header.range=20;header.patchids=0;
 init_patch_decompressor(16);set_group_of_patch_header(&group);
 float heights[256];decompress_patch(heights,result[0].coefficients.data(),&header);
 for(float h:heights)assert(std::abs(h-19.0625f)<.0001f);
 for(size_t n=0;n<bytes.size();++n) {auto truncated=bytes;truncated.resize(n);assert(!VSTerrain::decode(truncated,'L',result));}
 assert(result.size()==1); // Rejected input cannot replace the accepted output.
 assert(!VSTerrain::decode(bytes,'M',result));
 assert(!VSTerrain::decode(packet(16,'L',16*32),'L',result));
 assert(!VSTerrain::decode(packet(64,'L',0),'L',result));
 assert(VSTerrain::decode(packet(32,'M',(7u<<16)|7u),'M',result));
 assert(result[0].x==7&&result[0].y==7&&result[0].size==32);
 assert(!VSTerrain::decode(packet(32,'M',(8u<<16)),'M',result));
}
''')

if __name__ == '__main__':
    unittest.main()
