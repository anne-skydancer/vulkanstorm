"""Deterministic embedded glTF image inputs for actual native scene qualification."""
import base64,json,struct,zlib,hashlib
from functools import lru_cache
from pathlib import Path
COLORS=(((17,34,51,255),(68,85,102,255),(119,136,153,255),(170,187,204,255)),
        ((128,128,255,255),(136,120,250,255),(120,136,250,255),(128,128,255,255)),
        ((11,77,155,255),(22,88,166,255),(33,99,177,255),(44,111,188,255)),
        ((51,17,34,255),(102,68,85,255),(153,119,136,255),(204,170,187,255)),
        ((210,210,210,255),(180,180,180,255),(150,150,150,255),(120,120,120,255)))
def png(width,height,colors):
 def chunk(kind,body):return struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body)&0xffffffff)
 rows=[]
 for y in range(height):
  half=2 if y>=height//2 else 0
  rows.append(b'\0'+bytes(colors[half])*(width//2)+bytes(colors[half+1])*(width-width//2))
 return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b'')
def documents():
 data=bytearray();views=[];accessors=[]
 def attribute(values,kind,count,component=5126):
  offset=len(data);data.extend(values);views.append(dict(buffer=0,byteOffset=offset,byteLength=len(values)))
  accessors.append(dict(bufferView=len(views)-1,componentType=component,count=count,type=kind));return len(accessors)-1
 positions=((-0.5,0.,-0.5),(0.5,0.,-0.5),(0.5,0.,0.5),(-0.5,0.,0.5))
 attributes=dict(POSITION=attribute(struct.pack('<12f',*(c for p in positions for c in p)),'VEC3',4),
  NORMAL=attribute(struct.pack('<12f',*([0.,-1.,0.]*4)),'VEC3',4),
  TEXCOORD_0=attribute(struct.pack('<8f',0,0,1,0,1,1,0,1),'VEC2',4),
  TEXCOORD_1=attribute(struct.pack('<8f',.1,.2,.9,.2,.9,.8,.1,.8),'VEC2',4),
  TANGENT=attribute(struct.pack('<16f',*([1.,0.,0.,1.]*4)),'VEC4',4),
  COLOR_0=attribute(struct.pack('<16f',*([.8,.7,.6,1.]*4)),'VEC4',4),
  JOINTS_0=attribute(bytes(16),'VEC4',4,5121),
  WEIGHTS_0=attribute(struct.pack('<16f',*([1.,0.,0.,0.]*4)),'VEC4',4))
 indices=attribute(struct.pack('<6H',0,1,2,0,2,3),'SCALAR',6,5123)
 inverse=attribute(struct.pack('<16f',*(1. if i%5==0 else 0. for i in range(16))),'MAT4',1)
 times=attribute(struct.pack('<2f',0.,2.),'SCALAR',2)
 translations=attribute(struct.pack('<6f',0,0,0,.25,0,0),'VEC3',2)
 def texture(index,uv=0):return dict(index=index,texCoord=uv,extensions={'KHR_texture_transform':dict(offset=[.1,.2],rotation=.2,scale=[.5,.75])})
 material=dict(pbrMetallicRoughness=dict(baseColorTexture=texture(0),baseColorFactor=[.25,.5,.75,.8],metallicRoughnessTexture=texture(2),metallicFactor=.35,roughnessFactor=.65),
  normalTexture=dict(**texture(1,1),scale=.5),emissiveTexture=texture(3),emissiveFactor=[.1,.2,.3],
  occlusionTexture=dict(**texture(4,1),strength=.6),alphaMode='MASK',alphaCutoff=.4,doubleSided=True)
 document=dict(asset=dict(version='2.0'),extensionsUsed=['KHR_texture_transform'],scene=0,scenes=[dict(nodes=[0,1])],
  nodes=[dict(mesh=0,skin=0),dict()],skins=[dict(inverseBindMatrices=inverse,joints=[1],skeleton=1)],
  animations=[dict(samplers=[dict(input=times,output=translations,interpolation='LINEAR')],channels=[dict(sampler=0,target=dict(node=1,path='translation'))])],
  buffers=[dict(uri='data:application/octet-stream;base64,'+base64.b64encode(data).decode(),byteLength=len(data))],bufferViews=views,accessors=accessors,
  meshes=[dict(primitives=[dict(attributes=attributes,indices=indices,mode=4,material=0)])],materials=[material],
  samplers=[dict(minFilter=9986,magFilter=9728,wrapS=33648,wrapT=33071)],textures=[dict(source=i,sampler=0) for i in range(5)],
  images=[dict(uri='data:image/png;base64,'+base64.b64encode(png(1024,512,c)).decode()) for c in COLORS])

 replacement=json.loads(json.dumps(document))
 replacement['images']=[dict(uri='data:image/png;base64,'+base64.b64encode(png(1024,512,c[1:]+c[:1])).decode()) for c in COLORS]
 return {'embedded.gltf':document,'replacement.gltf':replacement}
@lru_cache(maxsize=1)
def inputs():return {name:(json.dumps(doc,sort_keys=True)+'\n').encode() for name,doc in documents().items()}
def write(directory):
 root=Path(directory);root.mkdir(parents=True,exist_ok=True)
 for name,data in inputs().items():(root/name).write_bytes(data)
def u64(value):
 if type(value) is int:return value
 data=base64.b64decode(value,validate=True)
 if len(data)!=8:raise ValueError('Invalid generation')
 return int.from_bytes(data,'big')
def contracts_passed(record,directory):
 try:
  root=Path(directory);data=record['r3_gltf_images']
  if any(data.get(key) is not True for key in ('object_retired','immutable_generations','account_retired')):return False
  for name,expected in inputs().items():
   if (root/'r3/gltf-images'/name).read_bytes()!=expected:return False
  generations=set()
  for phase in ('initial','full-resolution','limited-again','replacement','minimized','restored'):
   row=data[phase];width=row['width'] if phase=='minimized' else 1024 if phase=='full-resolution' else 512;height=width//2;variant=int(phase in ('replacement','minimized','restored'))
   if phase=='minimized' and (type(width) is not int or width not in (16,32,64,128,256) or row.get('bias',0)<3.9):return False
   if phase=='restored' and row.get('restored_window') is not True:return False
   if row['width']!=width or row['height']!=height or row['variant']!=variant or row['material_contracts'] is not True:return False
   if [image['channel'] for image in row['images']]!=list(range(5)):return False
   for channel,image in enumerate(row['images']):
    path=f'r3/gltf-images/{phase}-{channel}.rgba'
    if image['file']!=path:return False
    generation=u64(image['generation'])
    if generation<=0 or generation in generations:return False
    generations.add(generation)
    pixels=(root/path).read_bytes()
    if len(pixels)!=width*height*4:return False
    for y in range(height):
     half=2 if y>=height//2 else 0
     expected=bytes(COLORS[channel][(half+variant)%4])*(width//2)+bytes(COLORS[channel][(half+1+variant)%4])*(width//2)
     if pixels[y*width*4:(y+1)*width*4]!=expected:return False
  return True
 except (KeyError,ValueError,TypeError,OSError):return False
def required_files(record):
 return ['r3/gltf-images/'+name for name in inputs()]+[image['file'] for phase in ('initial','full-resolution','limited-again','replacement','minimized','restored') for image in record['r3_gltf_images'][phase]['images']]
if __name__=='__main__':write('.tmp/r3-gltf-fixture')
