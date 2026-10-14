"""Deterministic glTF inputs for the connected viewer's real scene decoder."""
import json
from pathlib import Path
import struct


def write(directory):
    root = Path(directory)
    positions = ((0., 0., 0.), (1., 0., 0.), (1., 1., 0.), (0., 1., 0.))
    data = b''.join(struct.pack('<3f', *p) for p in positions)
    data += struct.pack('<2f', 0., 2.)
    data += struct.pack('<6f', 0., 0., 0., 0., 0., 1.)
    data += struct.pack('<12f', *([0., 0., 1.] * 4))
    data += struct.pack('<16f', *([1., 0., 0., 1.] * 4))
    (root / 'geometry.bin').write_bytes(data)
    base = dict(asset=dict(version='2.0'), scene=0, scenes=[dict(nodes=[0])],
                nodes=[dict(translation=[2., 3., 4.], children=[1]), dict(mesh=0)],
                buffers=[dict(uri='geometry.bin', byteLength=len(data))],
                bufferViews=[dict(buffer=0, byteOffset=0, byteLength=48),
                             dict(buffer=0, byteOffset=48, byteLength=8),
                             dict(buffer=0, byteOffset=56, byteLength=24),
                             dict(buffer=0, byteOffset=80, byteLength=48),
                             dict(buffer=0, byteOffset=128, byteLength=64)],
                accessors=[dict(bufferView=0, componentType=5126, count=4, type='VEC3'),
                           dict(bufferView=1, componentType=5126, count=2, type='SCALAR'),
                           dict(bufferView=2, componentType=5126, count=2, type='VEC3'),
                           dict(bufferView=3, componentType=5126, count=4, type='VEC3'),
                           dict(bufferView=4, componentType=5126, count=4, type='VEC4')])
    # Triangle-list requires a multiple of three; other modes use the full quad.
    for mode in range(7):
        document = json.loads(json.dumps(base))
        document['accessors'][0]['count'] = 3 if mode == 4 else 4
        document['accessors'][3]['count'] = document['accessors'][0]['count']
        document['accessors'][4]['count'] = document['accessors'][0]['count']
        document['meshes'] = [dict(primitives=[dict(attributes=dict(POSITION=0, NORMAL=3, TANGENT=4), mode=mode)])]
        (root / f'topology-{mode}.gltf').write_text(json.dumps(document))
        if mode in (5, 6):
            document['meshes'][0]['primitives'][0]['attributes'] = dict(POSITION=0)
            (root / f'prepared-{mode}.gltf').write_text(json.dumps(document))
    animated = json.loads(json.dumps(base))
    animated['meshes'] = [dict(primitives=[dict(attributes=dict(POSITION=0, NORMAL=3, TANGENT=4), mode=6)])]
    animated['animations'] = [dict(samplers=[dict(input=1, output=2, interpolation='LINEAR')],
                                   channels=[dict(sampler=0, target=dict(node=1, path='translation'))])]
    (root / 'animated.gltf').write_text(json.dumps(animated))
    # Four vertices weighted to one real skin joint. Animate the joint rather
    # than the mesh node so the viewer must evaluate its actual matrix palette.
    skinned = json.loads(json.dumps(animated))
    skin_data = data + bytes(16) + struct.pack('<16f', *([1., 0., 0., 0.] * 4))
    skin_data += struct.pack('<16f', *(1. if i % 5 == 0 else 0. for i in range(16)))
    (root / 'skinned.bin').write_bytes(skin_data)
    skinned['buffers'] = [dict(uri='skinned.bin', byteLength=len(skin_data))]
    skinned['bufferViews'] += [dict(buffer=0, byteOffset=len(data), byteLength=16),
                               dict(buffer=0, byteOffset=len(data)+16, byteLength=64),
                               dict(buffer=0, byteOffset=len(data)+80, byteLength=64)]
    skinned['accessors'] += [dict(bufferView=5, componentType=5121, count=4, type='VEC4'),
                            dict(bufferView=6, componentType=5126, count=4, type='VEC4'),
                            dict(bufferView=7, componentType=5126, count=1, type='MAT4')]
    skinned['nodes'][0]['children'].append(2)
    skinned['nodes'][1]['skin'] = 0
    skinned['nodes'].append(dict(translation=[0., 0., 0.]))
    skinned['skins'] = [dict(joints=[2], inverseBindMatrices=7)]
    skinned['meshes'][0]['primitives'][0]['attributes'].update(JOINTS_0=5, WEIGHTS_0=6)
    skinned['animations'][0]['channels'][0]['target']['node'] = 2
    (root / 'skinned.gltf').write_text(json.dumps(skinned))
    empty = dict(asset=dict(version='2.0'), scene=0, scenes=[dict(nodes=[])], nodes=[])
    (root / 'empty.gltf').write_text(json.dumps(empty))
    # Invalid index must be rejected without replacing an admitted generation.
    invalid = json.loads(json.dumps(animated))
    invalid['nodes'][1]['mesh'] = 99
    (root / 'invalid.gltf').write_text(json.dumps(invalid))
