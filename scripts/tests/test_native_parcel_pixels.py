"""Parcel pixel expectations come from the original OpenGL strip, not a capture."""
import math
import re
import unittest
from native_source_fixture import ROOT, method


def reference_strip(heights):
    source=(ROOT/'indra/newview/llglsandbox.cpp').read_text(encoding='utf-8')
    body=method(source,'void LLViewerParcelMgr::renderRect(')
    strip=body.split('gGL.begin(LLRender::TRIANGLE_STRIP);',1)[1].split('gGL.end();',1)[0]
    height=float(re.search(r'PARCEL_POST_HEIGHT = ([.0-9]+)f;',source)[1])
    values=dict(west=4.,east=8.,south=4.,north=8.)
    for name,value in zip(('sw','se','ne','nw'),heights):
        values[name+'_bottom']=value;values[name+'_top']=value+height
    vertices=[tuple(values[name.strip()] for name in args.split(','))
              for args in re.findall(r'gGL.vertex3f\(([^)]+)\);',strip)]
    if len(vertices)!=10:raise AssertionError('Reference parcel strip changed')
    return vertices,height


def projected_layers(vertices,x,y):
    # Same camera, independently projected with the standard perspective equations.
    # Pixel rows use the intended upper-left readback convention.
    cot=1/math.tan(math.pi/6)
    points=[(cot*(px-6)/(py+4),cot*(pz-.333)/(py+4),py+4) for px,py,pz in vertices]
    sample=((x+.5)/32-1,1-(y+.5)/32)
    def edge(a,b,p):return (b[0]-a[0])*(p[1]-a[1])-(b[1]-a[1])*(p[0]-a[0])
    depth=math.inf;layers=0
    for i in range(8):
        a,b,c=points[i:i+3];area=edge(a,b,c)
        if abs(area)<1e-14:continue
        weights=(edge(b,c,sample)/area,edge(c,a,sample)/area,edge(a,b,sample)/area)
        if min(weights)<0:continue
        # Samples must be strictly inside; no device edge-fill ambiguity is admitted.
        if min(weights)<1e-6:raise AssertionError('Reference sample is on an edge')
        reciprocal=sum(w/p[2] for w,p in zip(weights,(a,b,c)))
        current=1/reciprocal
        if current<=depth+1e-6:layers+=1;depth=current
    return layers


def blended(layers):
    color=[255,0,255,255]
    for _ in range(layers):
        color=[round(.2*src+.8*dst) for src,dst in zip((255,255,0),color[:3])]+[255]
    return tuple(color)


class ParcelPixels(unittest.TestCase):
    def test_actual_native_strip_preserves_original_vertex_order_and_heights(self):
        source=(ROOT/'indra/newview/vsparcelpresentation.cpp').read_text(encoding='utf-8')
        corners=re.search(r'corners\{\{(.*?)\}\};',source,re.S)[1]
        expressions=[args.split(',') for args in re.findall(r'\{([^{}]+)\}',corners)]
        indices=[int(n) for n in re.search(r'strip\[\]=\{([^}]+)\}',source)[1].split(',')]
        self.assertEqual(len(expressions),8);self.assertEqual(len(indices),10)
        for heights in ((0,0,0,0),(1,4,-2,7),(-100,100,5,25)):
            reference,height=reference_strip(heights)
            values=dict(x1=4.,x2=8.,y1=4.,y2=8.,parcelPostHeight=height,
                        **dict(zip(('sw','se','ne','nw'),heights)))
            points=[tuple(sum(values[term.strip()] for term in expr.split('+')) for expr in row) for row in expressions]
            self.assertEqual([points[i] for i in indices],reference)

    def test_gpu_samples_cover_reference_one_two_three_layers_and_orientation(self):
        source=(ROOT/'indra/newview/vsparcelpresentation.cpp').read_text(encoding='utf-8')
        declaration=source.split('constexpr std::array<WallSample,3> wallSamples',1)[1].split(';',1)[0]
        samples=re.findall(r'\{(\d+),(\d+),(\d+),\{([^}]+)\}\}',declaration)
        self.assertEqual(len(samples),3)
        vertices,_=reference_strip((0,0,0,0));seen=set()
        for x,y,count,color in samples:
            x,y,count=map(int,(x,y,count));seen.add(count)
            self.assertEqual(projected_layers(vertices,x,y),count,(x,y))
            self.assertEqual(blended(count),tuple(map(int,color.split(','))))
        self.assertEqual(seen,{1,2,3})
        # Vertical reflection exchanges the one/three-layer samples and must fail.
        self.assertNotEqual(projected_layers(vertices,32,32),projected_layers(vertices,32,31))

if __name__=='__main__':unittest.main()
