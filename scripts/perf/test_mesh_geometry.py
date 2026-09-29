"""Validate production mesh attribute conversion on a real OpenGL driver.
Use --opengl PATH for packaged Mesa/Zink. No viewer login or user profile changes.
"""
from pathlib import Path
harness = Path(__file__).with_name('test_gl_compute_mesh.py').read_text()
exec(compile(harness[:harness.index('shader_path=')], str(Path(__file__)), 'exec'))
shader = Path(__file__).resolve().parents[2]/'indra/newview/app_settings/shaders/class1/objects/meshGeometryC.glsl'
compute=program([(0x91B9,shader.read_text())])
gen=fn('glGenBuffers',None,I,C.POINTER(U));bind=fn('glBindBuffer',None,U,U)
data=fn('glBufferData',None,U,S,P,U);base=fn('glBindBufferBase',None,U,U,U)
read=fn('glGetBufferSubData',None,U,S,S,P)
barrier=fn('glMemoryBarrier',None,U);dispatch=fn('glDispatchCompute',None,U,U,U)
uniform4=fn('glUniform4ui',None,I,U,U,U,U);location=fn('glGetUniformLocation',I,U,C.c_char_p)
matrix=fn('glUniformMatrix4fv',None,I,I,C.c_ubyte,C.POINTER(C.c_float))
get_error=fn('glGetError',U)
buffers=(U*2)();gen(2,buffers);SSBO=0x90D2
rng=random.Random(42)
checks=0
for count in (1,3,63,64,65,511):
    padded=(count+3)&~3
    source=[]
    for plane in range(3):
        for vertex in range(count):
            source.extend([rng.uniform(-5,5) for _ in range(3)]+[(-1. if vertex%2 else 1.) if plane==2 else 999.])
    raw=(C.c_float*len(source))(*source)
    source=list(raw)
    bind(SSBO,buffers[0]);data(SSBO,C.sizeof(raw),raw,0x88E4);base(SSBO,0,buffers[0])
    # Distinct offsets and guard words detect cross-face/attribute corruption.
    offsets=(5,5+padded+7,5+2*(padded+7))
    size=offsets[2]+padded+5
    for mask in range(4):
        for index in (0,15,8191):
            for scale in ((1.,1.,1.),(-2.,3.,.25)):
                output=(C.c_uint*(size*4))(*([0xdeadbeef]*(size*4)))
                bind(SSBO,buffers[1]);data(SSBO,C.sizeof(output),output,0x88E4);base(SSBO,1,buffers[1])
                pos=(C.c_float*16)(scale[0],0,0,0,0,scale[1],0,0,0,0,scale[2],0,7,-3,2,1)
                norm=(C.c_float*16)(1/scale[0],0,0,0,0,1/scale[1],0,0,0,0,1/scale[2],0,0,0,0,1)
                use(compute)
                matrix(location(compute,b'position_transform'),1,0,pos)
                matrix(location(compute,b'normal_transform'),1,0,norm)
                uniform4(location(compute,b'source_range'),count,count,padded,index)
                uniform4(location(compute,b'destination_range'),*offsets,mask)
                dispatch((padded+63)//64,1,1);barrier(0x200|1)
                read(SSBO,0,C.sizeof(output),output)
                floats=C.cast(output,C.POINTER(C.c_float))
                touched=set()
                for v in range(padded):
                    src=min(v,count-1)
                    for plane in range(3):
                        if plane and not (mask & (1<<(plane-1))): continue
                        offset=(offsets[plane]+v)*4
                        touched.update(range(offset,offset+4))
                        for axis in range(3):
                            value=source[(plane*count+src)*4+axis]
                            expected=value*scale[axis]+(7,-3,2)[axis] if plane==0 else value/scale[axis]
                            assert math.isclose(floats[offset+axis],expected,rel_tol=2e-6,abs_tol=2e-6),(plane,v,axis,floats[offset+axis],expected)
                            checks+=1
                        if plane==0: assert output[offset+3]==index
                        elif plane==1: assert floats[offset+3]==0
                        else: assert floats[offset+3]==source[(2*count+src)*4+3]
                assert all(output[i]==0xdeadbeef for i in range(len(output)) if i not in touched)
                assert get_error()==0
print(f'Mesh geometry: {checks} transform checks passed, including masks, handedness, padding and guards.')
uniform1=fn('glUniform1ui',None,I,U)
float4=fn('glUniform4f',None,I,C.c_float,C.c_float,C.c_float,C.c_float)
def select(op,inputs,outputs):
    use(compute);uniform1(location(compute,b'operation'),op)
    uniform4(location(compute,b'source_range'),*inputs)
    uniform4(location(compute,b'destination_range'),*outputs)
def allocate_output(words):
    output=(U*words)(*([0xdeadbeef]*words))
    bind(SSBO,buffers[1]);data(SSBO,C.sizeof(output),output,0x88E4);base(SSBO,1,buffers[1])
    return output
def finish(output,count):
    dispatch((count+63)//64,1,1);barrier(0x200|1|2)
    bind(SSBO,buffers[1]);read(SSBO,0,C.sizeof(output),output)
    assert get_error()==0
# Packed U16 indices: odd starts/ends, rebasing in both directions and neighboring faces.
for source_first in (0,1,7):
    for target_first in (0,1,8):
        for count in (1,2,3,63,64,65):
            values=[(i*173)%60000 for i in range(count+source_first+2)]
            payload=struct.pack('<'+'H'*len(values),*values)
            if len(payload)%4: payload+=b'\0\0'
            raw=C.create_string_buffer(payload);bind(SSBO,buffers[0]);data(SSBO,len(payload),raw,0x88E4);base(SSBO,0,buffers[0])
            for delta in (0,321,0xffffffff-100):
                output=allocate_output((target_first+count+3)//2)
                before=bytes(output)
                select(4,(source_first,count,count,delta),(target_first,0,0,0));finish(output,count)
                expected=bytearray(before)
                for i in range(count): struct.pack_into('<H',expected,(target_first+i)*2,(values[source_first+i]+delta)&65535)
                assert bytes(output)==expected
# Color/glow fill writes only the requested face range.
output=allocate_output(143);select(1,(0,65,65,0x07050301),(7,0,0,0));finish(output,65)
assert list(output)==[0xdeadbeef]*7+[0x07050301]*65+[0xdeadbeef]*71
# Shared immutable sources hold positions/normals/tangents, UVs, then weights.
count=65
positions=[(rng.uniform(-2,2),rng.uniform(-2,2),rng.uniform(-2,2),0) for _ in range(count)]
normals=[((1,0,0,0),(-1,0,0,0),(0,1,0,0),(0,-1,0,0),(0,0,1,0))[i%5] for i in range(count)]
tangents=[(1,0,0,(-1 if i%2 else 1)) for i in range(count)]
uvs=[(rng.uniform(-2,2),rng.uniform(-2,2)) for _ in range(count)]
weights=[(i+.125,i+1.25,i+2.375,i+3.25) for i in range(count)]
values=[v for plane in (positions,normals,tangents,uvs,weights) for item in plane for v in item]
raw=(C.c_float*len(values))(*values)
bind(SSBO,buffers[0]);data(SSBO,C.sizeof(raw),raw,0x88E4);base(SSBO,0,buffers[0])
output=allocate_output(count*4+12);select(2,(count,count,count,0),(7,0,0,0));finish(output,count)
assert bytes(output)[28:28+count*16]==bytes(raw)[count*56:count*72]
assert list(output)[:7]==[0xdeadbeef]*7 and list(output)[7+count*4:]==[0xdeadbeef]*5
identity=(C.c_float*16)(1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1)
texture=(C.c_float*16)(2,0,0,0,0,3,0,0,0,0,1,0,.25,-.5,0,1)
normal_matrix=(C.c_float*16)(.5,0,0,0,0,2,0,0,0,0,1,0,0,0,0,1)
rotation_matrix=(C.c_float*16)(0,1,0,0,-1,0,0,0,0,0,1,0,0,0,0,1)
for flags in range(32):
    output=allocate_output(count*2+16)
    select(3,(count,count,count,0),(9,0,0,0))
    matrix(location(compute,b'texture_transform'),1,0,texture)
    matrix(location(compute,b'normal_transform'),1,0,normal_matrix)
    matrix(location(compute,b'bump_rotation'),1,0,rotation_matrix)
    float4(location(compute,b'binormal_direction'),.6,.8,0,0)
    float4(location(compute,b'bump_s'),.125,-.25,.375,0)
    float4(location(compute,b'bump_t'),-.375,.25,.125,0)
    uniform1(location(compute,b'uv_flags'),flags)
    float4(location(compute,b'uv_scale'),2,3,.5,0)
    float4(location(compute,b'uv_transform'),.8,.6,2,3)
    float4(location(compute,b'uv_offset'),.25,-.5,0,0)
    finish(output,count)
    floats=C.cast(output,C.POINTER(C.c_float))
    for i in range(count):
        tc=uvs[i]
        if flags&1:
            n=normals[i];bn=(0,(-1 if n[0]<0 else 1),0) if abs(n[0])>=.5 else ((-1 if n[1]>0 else 1),0,0)
            tn=(bn[1]*n[2]-bn[2]*n[1],bn[2]*n[0]-bn[0]*n[2],bn[0]*n[1]-bn[1]*n[0])
            pt=[a*b for a,b in zip(positions[i][:3],(2,3,.5))]
            tc=(.5+2*sum(a*b for a,b in zip(bn,pt)),.5-2*sum(a*b for a,b in zip(tn,pt)))
        if flags&2: tc=(tc[0]*2+.25,tc[1]*3-.5)
        elif flags&4:
            a,b=tc[0]-.5,tc[1]-.5
            tc=((a*.8+b*.6)*2+.75,(-a*.6+b*.8)*3)
        if flags&8:
            n,t=normals[i],tangents[i]
            bt=((n[1]*t[2]-n[2]*t[1])*t[3],(n[2]*t[0]-n[0]*t[2])*t[3],(n[0]*t[1]-n[1]*t[0])*t[3])
            bn=[(t[j]*.6+bt[j]*.8)*(.5,2,1)[j] for j in range(3)]
            if flags&16: bn=[-bn[1],bn[0],bn[2]]
            length=math.sqrt(sum(v*v for v in bn))
            bn=[v/length for v in bn] if length else [0,0,0]
            tc=(tc[0]+sum(a*b for a,b in zip((.125,-.25,.375),t)),tc[1]+sum(a*b for a,b in zip((-.375,.25,.125),bn)))
        assert all(math.isclose(floats[9+i*2+j],tc[j],rel_tol=3e-6,abs_tol=3e-6) for j in range(2)),(flags,i,tc)
    assert list(output)[:9]==[0xdeadbeef]*9 and list(output)[9+count*2:]==[0xdeadbeef]*7
print('Packed indices, color/glow, weights, planar/default/emboss UVs, active rotation and matrix/TE precedence passed.')
# Keep geometry resident across texture/color updates and a neighboring CPU upload.
# This exercises actual byte preservation, not just initial sentinel writes.
padded=(count+3)&~3
position_offset=4
normal_offset=position_offset+padded+4
tangent_offset=normal_offset+padded+4
uv_offset=(tangent_offset+padded+4)*4
color_offset=uv_offset+count*2+8
cpu_offset=color_offset+padded+8
output=allocate_output(cpu_offset+16)
select(0,(count,count,padded,17),(position_offset,normal_offset,tangent_offset,3))
matrix(location(compute,b'position_transform'),1,0,identity)
matrix(location(compute,b'normal_transform'),1,0,normal_matrix)
finish(output,padded)
expected=list(output)
subdata=fn('glBufferSubData',None,U,S,S,P)
cpu_words=(U*8)(*[0xabc00000+i for i in range(8)])
for iteration in range(20):
    select(3,(count,count,count,0),(uv_offset,0,0,0))
    uniform1(location(compute,b'uv_flags'),2)
    texture[12]=iteration*.125
    matrix(location(compute,b'texture_transform'),1,0,texture)
    dispatch((count+63)//64,1,1);barrier(0x2000)
    select(1,(0,padded,padded,0xff000000+iteration),(color_offset,0,0,0))
    dispatch((padded+63)//64,1,1);barrier(0x200|1)
    bind(SSBO,buffers[1]);subdata(SSBO,cpu_offset*4,C.sizeof(cpu_words),cpu_words)
    read(SSBO,0,C.sizeof(output),output)
    floats=C.cast(output,C.POINTER(C.c_float))
    for i in range(count):
        assert math.isclose(floats[uv_offset+i*2],uvs[i][0]*2+texture[12],rel_tol=3e-6,abs_tol=3e-6)
        assert math.isclose(floats[uv_offset+i*2+1],uvs[i][1]*3-.5,rel_tol=3e-6,abs_tol=3e-6)
    assert list(output)[color_offset:color_offset+padded]==[0xff000000+iteration]*padded
    assert list(output)[cpu_offset:cpu_offset+8]==list(cpu_words)
    mutable=set(range(uv_offset,uv_offset+count*2))|set(range(color_offset,color_offset+padded))|set(range(cpu_offset,cpu_offset+8))
    assert all(output[i]==expected[i] for i in range(len(output)) if i not in mutable)
    assert get_error()==0
print('20 texture/color rebuilds and neighboring CPU uploads preserve resident geometry and guards.')
# Ordered command expansion retains zero draws and non-monotonic offsets.
for count in (2,63,64,65,256):
    ranges=[v for i in range(count) for v in (i*3,(count-i)*17)]
    raw=(U*len(ranges))(*ranges)
    bind(SSBO,buffers[0]);data(SSBO,C.sizeof(raw),raw,0x88E4);base(SSBO,0,buffers[0])
    output=allocate_output(count*5+7)
    select(5,(0,count,count,0),(0,0,0,0));finish(output,count)
    assert list(output)[:count*5]==[v for i in range(count) for v in (i*3,1,(count-i)*17,0,0)]
    assert list(output)[count*5:]==[0xdeadbeef]*7
print('Ordered indirect command expansion and guards passed.')

gl.wglMakeCurrent(None,None);gl.wglDeleteContext(context);user.ReleaseDC(window,hdc);user.DestroyWindow(window)
